#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    size_t curr_window_size = _received_window ? _received_window : 1;
    // 循环填充窗口
    while (curr_window_size > _bytes_in_flight){
        TCPSegment seg;
        TCPHeader& header = seg.header();

        // 1) SYN
        if (!_syn_sent) {
            header.syn = true;
            _syn_sent = true;
        } 
        // 2) 正常数据
        header.seqno = wrap(_next_seqno, _isn);
        size_t payload_size = min( curr_window_size - _bytes_in_flight - seg.header().syn, TCPConfig::MAX_PAYLOAD_SIZE);
        string payload = stream_in().read(payload_size);

        // 3) FIN（当EOF且还有空间且尚未发送过）
        // 含SYN/FIN seg.length_in_sequence_space();
        bool can_fin = stream_in().eof() && !_fin_sent && payload.size() + _bytes_in_flight < curr_window_size;
        if (can_fin) {
            seg.header().fin = true;
            _fin_sent = true;
        }
        
        seg.payload() = Buffer(std::move(payload));

        // 若段完全为空（既无SYN/FIN也无payload），停止
        if (seg.length_in_sequence_space() == 0) 
            break;

        // 序号推进 & 记账
        if (_outstanding_segments.empty()) {
            _current_retransmission_timeout = _initial_retransmission_timeout;
            _timeout_count = 0;
        }
        // 发送
        _segments_out.push(seg);
        // if(seg.length_in_sequence_space() > 0) 无影响
        _outstanding_segments.insert(make_pair(_next_seqno, seg));
        _bytes_in_flight += seg.length_in_sequence_space();
        _next_seqno += seg.length_in_sequence_space();
        
        // 循环继续，直到无空间或无数据可读且不可发FIN
        if (seg.header().fin) 
            break;
  }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) { 
    // unwrap 相对 _next_seqno，以避免倒退
    const uint64_t ack_abs = unwrap(ackno, _isn, _next_seqno);

    // 越界ACK（超前于已发送序号）→ ignore“推进”
    if (ack_abs > _next_seqno) 
        return; 
    // 遍历数据结构，将已经接收到的数据包丢弃
    for (auto iter = _outstanding_segments.begin(); iter != _outstanding_segments.end();) {
        // 如果一个发送的数据包已经被成功接收
        const TCPSegment &seg = iter->second;
        if (iter->first + seg.length_in_sequence_space() <= ack_abs) {
            _bytes_in_flight -= seg.length_in_sequence_space();
            iter = _outstanding_segments.erase(iter);

            // 如果有新的数据包被成功接收，则清空超时时间
            _current_retransmission_timeout = _initial_retransmission_timeout;
            _timeout_count = 0;
        }
        // 如果当前遍历到的数据包还没被接收，则说明后面的数据包均未被接收，因此直接返回
        else
            break;
    }
    // 每次收到ACK都重置计数器
    _consecutive_retx = 0;
    // 填充后面的数据
    _received_window = window_size;
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    _timeout_count += ms_since_last_tick;

    auto iter = _outstanding_segments.begin();
    // 如果存在发送中的数据包，并且定时器超时
    if (iter != _outstanding_segments.end() && _timeout_count >= _current_retransmission_timeout) {
        // 如果窗口大小不为0还超时，则说明网络拥堵
        if (_received_window > 0)
            _current_retransmission_timeout *= 2;
        _timeout_count = 0;
        _segments_out.push(iter->second);
        // 连续重传计时器增加
        ++_consecutive_retx;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retx; }

void TCPSender::send_empty_segment() {
    TCPSegment segment;
    segment.header().seqno = next_seqno();
    _segments_out.push(segment);
}
