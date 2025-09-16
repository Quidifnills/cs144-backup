#include "tcp_connection.hh"
#include <iostream>
#include <limits>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) { 
    // 每收到一个segment, 重置计时器
    _time_since_last_segment_received = 0;
    // 是否需要发送一个不占序列空间的空 ack 包，因为收到任何占序列空间的 TCP 段都需要 ack，或者 keep-alive 也需要空 ack 包
    bool need_empty_ack = seg.length_in_sequence_space() > 0;
    
    // RST Flag is set
    if(seg.header().rst){
        _receiver.stream_out().set_error();
        _sender.stream_in().set_error();
        _linger_after_streams_finish = false;
        _active = false;
        return;
    }
    // 处理收到的seg
    _receiver.segment_received(seg);

    // ACK Flag is set
    if(seg.header().ack){
        _sender.ack_received(seg.header().ackno, seg.header().win);
        // _sender.fill_window(); // 这行其实是多余的，因为已经在 ack_received 中被调用了，不过这里显示说明一下其操作
        // 如果原本需要发送空ack，并且此时 sender 发送了新数据，则停止发送空ack
        if (need_empty_ack && !_sender.segments_out().empty())
            need_empty_ack = false;
    }
    //_sender.fill_window(); // called in _sender.ack_received()
    // 如果是 LISEN 到了 SYN
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::SYN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::CLOSED) {
        // 此时肯定是第一次调用 fill_window，因此会发送 SYN + ACK
        connect();
        return;
    }

    // 判断 TCP 断开连接时是否时需要等待
    // CLOSE_WAIT
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_ACKED)
        _linger_after_streams_finish = false;

    // 如果到了准备断开连接的时候。服务器端先断
    // CLOSED
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED && !_linger_after_streams_finish) {
        _active = false;
        return;
    }
    // Handle the linger_after_streams_finish flag
    // If inbound stream ends before we've sent FIN, don't linger
    // Peer has ended: _receiver.stream_out().input_ended() = true
    // We have not ended: _sender.stream_in().eof() = true
    if (_receiver.stream_out().input_ended() && !_sender.stream_in().eof()) {
        _linger_after_streams_finish = false;
    }

    // if (seg.length_in_sequence_space() > 0 && _sender.segments_out().empty()) {
    //     _sender.send_empty_segment();
    // }

    // 5) keep-alive 特判：对端用 seq=ack-1 且 length==0 探活，也要回一个空段
    if (_receiver.ackno().has_value()
        && seg.length_in_sequence_space() == 0
        && seg.header().seqno == _receiver.ackno().value() - 1) {
        need_empty_ack = true;
    }

     // 如果收到的数据包里没有任何数据，则这个数据包可能只是为了 keep-alive
    if (need_empty_ack)
        _sender.send_empty_segment();

    // 添加ACK和WIN，发送包
    send_segment_with_ack_win();
 }

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
    size_t bytes_written = _sender.stream_in().write(data);
    // Tell the sender to fill the window to send any available data
    _sender.fill_window();
    send_segment_with_ack_win();
    return bytes_written;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) { 

    // Tell the sender passage of time
    _sender.tick(ms_since_last_tick);

    //  Abort the connection, and send a reset segment to the peer (an empty segment with
    //  the rst flag set), if the number of consecutive retransmissions is more than an upper
    //  limit TCPConfig::MAX_RETX_ATTEMPTS.
    if(_sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS){
         // 清除本应该重发的包
        while (!_sender.segments_out().empty()) _sender.segments_out().pop();
        // Abort the connection and send a reset segment
        // First, generate an empty segment with proper sequence number
        _sender.send_empty_segment();
        // Get the segment and set the RST flag
        if (!_sender.segments_out().empty()) {
            TCPSegment rst_segment = _sender.segments_out().front();
            _sender.segments_out().pop();
            rst_segment.header().rst = true;
            
            // // Set receiver fields if we have an ackno
            // if (_receiver.ackno().has_value()) {
            //     rst_segment.header().ack = true;
            //     rst_segment.header().ackno = _receiver.ackno().value();
            //     rst_segment.header().win = _receiver.window_size();
            // }

            // Send the reset segment
            _segments_out.push(move(rst_segment));
        }
    
        // Set error
        _receiver.stream_out().set_error();
        _sender.stream_in().set_error();
        _active = false; 
        _linger_after_streams_finish = false;

        // Brutally end the connection 
        return;
    }

    // If we didn't hit the abort condition above
    send_segment_with_ack_win();

    _time_since_last_segment_received += ms_since_last_tick;

    // End the connection cleanly
    const bool inbound_finished  = _receiver.stream_out().input_ended(); 
    const bool app_ended_out     = _sender.stream_in().eof();            
    const bool all_acked         = _sender.bytes_in_flight() == 0;       

    if (inbound_finished && app_ended_out && all_acked) {
        if (!_linger_after_streams_finish) {
            // 被动关闭：无需等待
            _active = false;
        } else if (_time_since_last_segment_received >= 10 * _cfg.rt_timeout) {
            // 主动关闭：已在 TIME-WAIT/linger 等够 10×RTO
            _active = false;
        }
    }
}
void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    // 在输入流结束后，必须立即发送 FIN
    _sender.fill_window();
    send_segment_with_ack_win();
}

void TCPConnection::connect() {
    // Initiate a connection by sending a SYN segment
    _sender.fill_window();
    send_segment_with_ack_win();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            // cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            // Send a RST segment to abort the connection
            // Generate an empty segment with proper sequence number
            _sender.send_empty_segment();
            
            // Get the segment and set the RST flag
            if (!_sender.segments_out().empty()) {
                TCPSegment rst_segment = _sender.segments_out().front();
                _sender.segments_out().pop();
                rst_segment.header().rst = true;
                
                // Set receiver fields if we have an ackno
                if (_receiver.ackno().has_value()) {
                    rst_segment.header().ack = true;
                    rst_segment.header().ackno = _receiver.ackno().value();
                    rst_segment.header().win = _receiver.window_size();
                }
                
                // Send the reset segment
                _segments_out.push(move(rst_segment));
            }
            
            // Set both streams to error state and kill the connection
            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();
            _active = false;
            _linger_after_streams_finish = false;
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

void TCPConnection::send_segment_with_ack_win(){
    // Send all the segments
    while (!_sender.segments_out().empty()) {
        TCPSegment segment = move(_sender.segments_out().front());
        _sender.segments_out().pop();

        if (_receiver.ackno().has_value()) {
            segment.header().ack = true;
            segment.header().ackno = _receiver.ackno().value();
            // segment.header().win = _receiver.window_size();
        }
        segment.header().win = min(static_cast<size_t>(numeric_limits<uint16_t>::max()), _receiver.window_size());
        // 这里不需要专门设置 RST：若你要发送 RST，通常先让 sender 生成空段，再手动把 rst 位置 1
        _segments_out.push(move(segment));  // TCPConnection 对外的发送队列
    }
}
