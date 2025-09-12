#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`
#include <algorithm>
#include <cassert>
template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : 
    _output(capacity), 
    _capacity(capacity),
    _segments(),
    _pending_bytes(0),  //!< total bytes in _segments
    _first_unassembled(0),  //!< next index needed
    _eof_index()
    {}
// Trim the assembly window to keep memory <= capacity and close if done.
void StreamReassembler::try_close_if_done_() {
    if (_eof_index.has_value() && _first_unassembled == *_eof_index) {
        _output.end_input();
    }
}
//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    if (eof) {
        const uint64_t end = index + data.size();
        if (_eof_index.has_value())
            _eof_index = min(*_eof_index, end);
        else
            _eof_index = end;
    }

    if (data.empty()) {
        try_close_if_done_();
        return;
    }

    // Compute assembly window: [first_unassembled, first_unassembled + window_size)
    // window_size = capacity - bytes currently buffered in ByteStream
    const uint64_t window_begin = _first_unassembled;
    const uint64_t buffered = _output.buffer_size(); // bytes already in ByteStream (counted against capacity)
    const uint64_t window_size =
        (_capacity > buffered) ? (_capacity - buffered) : 0;
    const uint64_t window_end = window_begin + window_size;

    // Discard outside-window portions; also drop already-assembled left side
    uint64_t seg_l = index;
    uint64_t seg_r = index + data.size();

    // clip to [window_begin, window_end)
    if (seg_r <= window_begin || seg_l >= window_end) {
        try_close_if_done_();
        return; // completely outside window
    }
    seg_l = max(seg_l, window_begin);
    seg_r = min(seg_r, window_end);

    // if (seg_l >= seg_r) {
    //     try_close_if_done_();
    //     return;
    // }
    // // Now clip off already-assembled prefix (redundant with window_begin, but harmless)
    // if (seg_r <= _first_unassembled) {
    //     try_close_if_done_();
    //     return;
    // }
    // if (seg_l < _first_unassembled) seg_l = _first_unassembled;

    // Extract the kept substring
    string piece = data.substr(seg_l - index, seg_r - seg_l);
    if (piece.empty()) {
        try_close_if_done_();
        return;
    }

    // Insert into _segments and merge with neighbors, maintaining non-overlapping invariant
    uint64_t L = seg_l, R = seg_r;
    auto it = _segments.lower_bound(L);

    // Merge with left neighbor if overlaps/touches
    if (it != _segments.begin()) {
        auto prev = std::prev(it);
        const uint64_t l0 = prev->first;
        const uint64_t r0 = l0 + prev->second.size();
        if (r0 >= L) {
            // Extend to cover [min(l0,L), max(r0,R)]
            if (l0 < L) {
                piece.insert(0, prev->second.substr(0, L - l0));
                L = l0;
            }
            if (r0 > R) {
                piece.append(prev->second.substr(R - l0));
                R = r0;
            }
            _pending_bytes -= prev->second.size();
            it = _segments.erase(prev);
        }
    }

    // Merge with subsequent overlapping/touching segments
    while (it != _segments.end()) {
        const uint64_t l1 = it->first;
        const uint64_t r1 = l1 + it->second.size();
        if (l1 > R) break; // disjoint
        // merge
        if (l1 < L) {
            piece.insert(0, it->second.substr(0, L - l1));
            L = l1;
        }
        if (r1 > R) {
            piece.append(it->second.substr(R - l1));
            R = r1;
        }
        _pending_bytes -= it->second.size();
        it = _segments.erase(it);
    }

    // Insert merged segment
    _pending_bytes += piece.size();
    _segments.emplace(L, std::move(piece));

    // Try to push from first_unassembled_ forward
    while (true) {
        auto hit = _segments.find(_first_unassembled);
        if (hit == _segments.end()) break;

        const string &seg = hit->second;

        // We trimmed to the window earlier, so this should fully fit
        const size_t written = _output.write(seg);
        // Defensive: in case ByteStream enforces remaining_capacity strictly,
        // written should equal seg.size(). If not, it still keeps invariants.
        _first_unassembled += written;
        _pending_bytes -= written;

        if (written == hit->second.size()) {
            _segments.erase(hit);
        } else {
            // Partial write (extremely unlikely due to our windowing). Keep the rest.
            _segments.erase(hit);
            const string rest = seg.substr(written);
            _segments.emplace(_first_unassembled, rest);
            _pending_bytes += rest.size();
            break;
        }
    }

    // Maybe we can close now
    try_close_if_done_();
}

size_t StreamReassembler::unassembled_bytes() const { return _pending_bytes; }

bool StreamReassembler::empty() const { return _pending_bytes == 0; }
