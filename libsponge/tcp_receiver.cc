#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    const TCPHeader &header = seg.header();
    if (!_syn_seen) {
        if (!header.syn) return;
        _syn_seen = true;
        _isn = header.seqno; // 记录初始序号(32-bit)
    }
    // 2) 计算 checkpoint（绝对序号坐标系下“附近”的参考点）
    //    = 已经写入到ByteStream的字节数 + 1(因为SYN占一个序号格)
    const uint64_t checkpoint = _reassembler.stream_out().bytes_written() + 1;

    // 3) 把 32-bit seqno 解包成 64-bit 绝对序号（对应段头序号）
    const uint64_t header_abs = unwrap(header.seqno, _isn, checkpoint);

    // 4) 该段payload的首字节对应的 stream index（不含SYN，所以要 -1；若带SYN，再+1）
    //    data_first = header_abs - 1 + (h.syn ? 1 : 0)
    const uint64_t data_first = header_abs + (header.syn ? 1 : 0) - 1;

    // 5) 取出payload，投喂 Reassembler。若带FIN，告诉reassembler这是最后位置。
    const string payload = seg.payload().copy();
    _reassembler.push_substring(payload, data_first, header.fin);
}

optional<WrappingInt32> TCPReceiver::ackno() const { 
    if (!_syn_seen) return nullopt;

    // 基本ACK = 已经写入ByteStream的字节数 + 1 (SYN占位)
    uint64_t abs_ack = _reassembler.stream_out().bytes_written() + 1;

    // 如果已经接收并写到末尾（包括收到FIN并且都拼接完），ACK 还要再+1（为 FIN 占位）
    if (_reassembler.stream_out().input_ended()) {
        abs_ack += 1;
    }

    return wrap(abs_ack, _isn);
 }

size_t TCPReceiver::window_size() const { return _capacity - _reassembler.stream_out().buffer_size(); }
