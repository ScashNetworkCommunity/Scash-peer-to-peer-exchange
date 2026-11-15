// Copyright (c) 2026 The Scash Network Community developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "exchangemulticoin.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <sstream>

#if !defined(SCASH_DEX_STANDALONE_TEST)
#include <key.h>
#include <pubkey.h>
#include <span.h>
#include <uint256.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#else
#include <QByteArray>
#include <QProcess>
#include <QString>
#include <QStringList>
#endif
#endif

namespace scash_dex {

namespace {

constexpr uint64_t RotL64(uint64_t x, unsigned int n)
{
    return (x << n) | (x >> (64U - n));
}

void KeccakF1600(uint64_t st[25])
{
    static constexpr uint64_t kRc[24] = {
        0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL, 0x8000000080008000ULL,
        0x000000000000808bULL, 0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL,
        0x000000000000008aULL, 0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
        0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL, 0x8000000000008003ULL,
        0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800aULL, 0x800000008000000aULL,
        0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL};
    static constexpr unsigned int kRho[24] = {
        1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14,
        27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44};
    static constexpr int kPi[24] = {
        10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4,
        15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1};

    for (int round = 0; round < 24; ++round) {
        uint64_t bc[5]{};
        for (int i = 0; i < 5; ++i) {
            bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];
        }
        for (int i = 0; i < 5; ++i) {
            const uint64_t t = bc[(i + 4) % 5] ^ RotL64(bc[(i + 1) % 5], 1);
            for (int j = 0; j < 25; j += 5) {
                st[j + i] ^= t;
            }
        }
        uint64_t t = st[1];
        for (int i = 0; i < 24; ++i) {
            const int j = kPi[i];
            const uint64_t tmp = st[j];
            st[j] = RotL64(t, kRho[i]);
            t = tmp;
        }
        for (int j = 0; j < 25; j += 5) {
            uint64_t row[5]{st[j], st[j + 1], st[j + 2], st[j + 3], st[j + 4]};
            for (int i = 0; i < 5; ++i) {
                st[j + i] ^= (~row[(i + 1) % 5]) & row[(i + 2) % 5];
            }
        }
        st[0] ^= kRc[round];
    }
}

uint32_t Bech32PolymodLocal(const std::vector<uint8_t>& values)
{
    uint32_t chk = 1;
    for (uint8_t v : values) {
        const uint8_t top = static_cast<uint8_t>(chk >> 25);
        chk = ((chk & 0x1ffffffUL) << 5) ^ v;
        if (top & 1) chk ^= 0x3b6a57b2UL;
        if (top & 2) chk ^= 0x26508e6dUL;
        if (top & 4) chk ^= 0x1ea119faUL;
        if (top & 8) chk ^= 0x3d4233ddUL;
        if (top & 16) chk ^= 0x2a1462b3UL;
    }
    return chk;
}

std::vector<uint8_t> ExpandHrpLocal(const std::string& hrp)
{
    std::vector<uint8_t> ret;
    ret.reserve(hrp.size() * 2 + 1);
    for (char c : hrp) ret.push_back(static_cast<uint8_t>(c) >> 5);
    ret.push_back(0);
    for (char c : hrp) ret.push_back(static_cast<uint8_t>(c) & 0x1f);
    return ret;
}

std::array<uint8_t, 32> Sha256dLocal(const std::vector<uint8_t>& buf)
{
    const auto h1 = Sha256Bytes(buf);
    return Sha256Bytes(h1.data(), h1.size());
}

void AppendLE32(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

void AppendLE64(std::vector<uint8_t>& out, uint64_t v)
{
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
    }
}

void AppendVarInt(std::vector<uint8_t>& out, uint64_t v)
{
    if (v < 0xfdULL) {
        out.push_back(static_cast<uint8_t>(v));
    } else if (v <= 0xffffULL) {
        out.push_back(0xfd);
        out.push_back(static_cast<uint8_t>(v & 0xff));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    } else if (v <= 0xffffffffULL) {
        out.push_back(0xfe);
        AppendLE32(out, static_cast<uint32_t>(v));
    } else {
        out.push_back(0xff);
        AppendLE64(out, v);
    }
}

std::string EncodeBase58(const std::vector<uint8_t>& input)
{
    static constexpr char kAlphabet[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    size_t zeros = 0;
    while (zeros < input.size() && input[zeros] == 0) ++zeros;

    std::vector<uint8_t> b58((input.size() - zeros) * 138 / 100 + 1, 0);
    size_t length = 0;
    for (size_t i = zeros; i < input.size(); ++i) {
        int carry = input[i];
        size_t j = 0;
        for (auto it = b58.rbegin(); (carry != 0 || j < length) && it != b58.rend(); ++it, ++j) {
            carry += 256 * (*it);
            *it = static_cast<uint8_t>(carry % 58);
            carry /= 58;
        }
        length = j;
    }
    auto it = b58.begin() + (b58.size() - length);
    while (it != b58.end() && *it == 0) ++it;

    std::string str(zeros, '1');
    while (it != b58.end()) {
        str.push_back(kAlphabet[*it]);
        ++it;
    }
    return str;
}

void EncodeRlpLength(std::vector<uint8_t>& out, size_t len, uint8_t offset)
{
    if (len < 56) {
        out.push_back(static_cast<uint8_t>(offset + len));
    } else {
        std::vector<uint8_t> len_bytes;
        size_t tmp = len;
        while (tmp > 0) {
            len_bytes.insert(len_bytes.begin(), static_cast<uint8_t>(tmp & 0xff));
            tmp >>= 8;
        }
        out.push_back(static_cast<uint8_t>(offset + 55 + len_bytes.size()));
        out.insert(out.end(), len_bytes.begin(), len_bytes.end());
    }
}

std::string Strip0x(const std::string& s)
{
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return s.substr(2);
    return s;
}

std::string Pad64Left(const std::string& hex_str)
{
    const std::string clean = Strip0x(hex_str);
    if (clean.size() >= 64) return clean.substr(clean.size() - 64);
    return std::string(64 - clean.size(), '0') + clean;
}

} // namespace

std::array<uint8_t, 32> Keccak256Bytes(const uint8_t* data, size_t len)
{
    constexpr size_t kRate = 136; // 1088 bits for Keccak-256
    uint64_t state[25]{};
    uint8_t block[kRate]{};

    size_t offset = 0;
    while (len - offset >= kRate) {
        for (size_t i = 0; i < kRate / 8; ++i) {
            uint64_t lane = 0;
            for (size_t b = 0; b < 8; ++b) {
                lane |= static_cast<uint64_t>(data[offset + i * 8 + b]) << (8 * b);
            }
            state[i] ^= lane;
        }
        KeccakF1600(state);
        offset += kRate;
    }

    const size_t rem = len - offset;
    if (rem > 0) {
        std::memcpy(block, data + offset, rem);
    }
    block[rem] ^= 0x01;          // Ethereum Keccak-256 domain padding (0x01, NOT 0x06)
    block[kRate - 1] ^= 0x80;

    for (size_t i = 0; i < kRate / 8; ++i) {
        uint64_t lane = 0;
        for (size_t b = 0; b < 8; ++b) {
            lane |= static_cast<uint64_t>(block[i * 8 + b]) << (8 * b);
        }
        state[i] ^= lane;
    }
    KeccakF1600(state);

    std::array<uint8_t, 32> out{};
    for (size_t i = 0; i < 4; ++i) {
        for (size_t b = 0; b < 8; ++b) {
            out[i * 8 + b] = static_cast<uint8_t>((state[i] >> (8 * b)) & 0xff);
        }
    }
    return out;
}

std::array<uint8_t, 32> Keccak256Bytes(const std::vector<uint8_t>& data)
{
    return Keccak256Bytes(data.data(), data.size());
}

std::string DeriveEvmAddressFromUncompressedPubkey(const std::vector<uint8_t>& uncompressed_pub65)
{
    if (uncompressed_pub65.size() != 65 || uncompressed_pub65[0] != 0x04) {
        return {};
    }
    const auto hash = Keccak256Bytes(uncompressed_pub65.data() + 1, 64);
    const std::string lower_hex = HexEncode(hash.data() + 12, 20);
    const auto addr_hash = Keccak256Bytes(
        reinterpret_cast<const uint8_t*>(lower_hex.data()), lower_hex.size());
    const std::string hash_hex = HexEncode(addr_hash.data(), addr_hash.size());

    std::string out = "0x";
    out.reserve(42);
    for (size_t i = 0; i < 40; ++i) {
        const char c = lower_hex[i];
        const char h = hash_hex[i];
        const int nibble = (h >= '0' && h <= '9') ? (h - '0') : (h - 'a' + 10);
        if (c >= 'a' && c <= 'f' && nibble >= 8) {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string DeriveBtcP2wpkhAddress(const std::vector<uint8_t>& compressed_pub33, const std::string& hrp)
{
    if (compressed_pub33.size() != 33) return {};
    const auto pkh20 = Hash160Bytes(compressed_pub33);

    static constexpr char kCharset[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
    std::vector<uint8_t> data5;
    data5.reserve(1 + 32);
    data5.push_back(0); // Witness version 0

    uint32_t acc = 0;
    int bits = 0;
    for (uint8_t byte : pkh20) {
        acc = (acc << 8) | byte;
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            data5.push_back(static_cast<uint8_t>((acc >> bits) & 0x1f));
        }
    }
    if (bits > 0) {
        data5.push_back(static_cast<uint8_t>((acc << (5 - bits)) & 0x1f));
    }

    std::vector<uint8_t> chk_in = ExpandHrpLocal(hrp);
    chk_in.insert(chk_in.end(), data5.begin(), data5.end());
    chk_in.insert(chk_in.end(), 6, 0);
    const uint32_t mod = Bech32PolymodLocal(chk_in) ^ 1;

    std::string ret = hrp + "1";
    for (uint8_t v : data5) ret.push_back(kCharset[v]);
    for (int i = 0; i < 6; ++i) {
        ret.push_back(kCharset[(mod >> (5 * (5 - i))) & 0x1f]);
    }
    return ret;
}

std::string EncodeBitcoinWifCompressed(const std::array<uint8_t, 32>& privkey32)
{
    std::vector<uint8_t> payload;
    payload.reserve(38);
    payload.push_back(0x80); // Bitcoin Mainnet WIF prefix
    payload.insert(payload.end(), privkey32.begin(), privkey32.end());
    payload.push_back(0x01); // Compressed pubkey flag
    const auto chk = Sha256dLocal(payload);
    payload.insert(payload.end(), chk.begin(), chk.begin() + 4);
    return EncodeBase58(payload);
}

std::vector<uint8_t> RlpEncodeBytes(const std::vector<uint8_t>& data)
{
    if (data.size() == 1 && data[0] < 0x80) {
        return {data[0]};
    }
    std::vector<uint8_t> out;
    EncodeRlpLength(out, data.size(), 0x80);
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

std::vector<uint8_t> RlpEncodeBigEndianTrimmed(const uint8_t* bytes, size_t len)
{
    size_t idx = 0;
    while (idx < len && bytes[idx] == 0) ++idx;
    if (idx == len) {
        return {0x80}; // RLP integer 0 is empty byte string `0x80`
    }
    std::vector<uint8_t> trimmed(bytes + idx, bytes + len);
    return RlpEncodeBytes(trimmed);
}

std::vector<uint8_t> RlpEncodeUint64(uint64_t val)
{
    if (val == 0) {
        return {0x80};
    }
    uint8_t be[8]{};
    for (int i = 7; i >= 0; --i) {
        be[i] = static_cast<uint8_t>(val & 0xff);
        val >>= 8;
    }
    return RlpEncodeBigEndianTrimmed(be, 8);
}

std::vector<uint8_t> RlpEncodeList(const std::vector<std::vector<uint8_t>>& items)
{
    size_t total = 0;
    for (const auto& item : items) total += item.size();
    std::vector<uint8_t> out;
    out.reserve(total + 9);
    EncodeRlpLength(out, total, 0xc0);
    for (const auto& item : items) {
        out.insert(out.end(), item.begin(), item.end());
    }
    return out;
}

std::array<uint8_t, 32> ComputeEip155SigningHash(
    uint64_t nonce,
    uint64_t gas_price_wei,
    uint64_t gas_limit,
    const std::string& to_addr_hex,
    uint64_t value_wei,
    const std::vector<uint8_t>& data,
    uint64_t chain_id)
{
    std::vector<uint8_t> to_bytes;
    if (!Strip0x(to_addr_hex).empty()) {
        const auto dec = HexDecode(to_addr_hex);
        if (dec && dec->size() == 20) to_bytes = *dec;
    }

    const auto rlp = RlpEncodeList({
        RlpEncodeUint64(nonce),
        RlpEncodeUint64(gas_price_wei),
        RlpEncodeUint64(gas_limit),
        RlpEncodeBytes(to_bytes),
        RlpEncodeUint64(value_wei),
        RlpEncodeBytes(data),
        RlpEncodeUint64(chain_id),
        RlpEncodeUint64(0),
        RlpEncodeUint64(0),
    });
    return Keccak256Bytes(rlp);
}

std::string AssembleSignedEip155TxHex(
    uint64_t nonce,
    uint64_t gas_price_wei,
    uint64_t gas_limit,
    const std::string& to_addr_hex,
    uint64_t value_wei,
    const std::vector<uint8_t>& data,
    uint64_t chain_id,
    const std::vector<uint8_t>& compact_sig65)
{
    if (compact_sig65.size() != 65) return {};
    std::vector<uint8_t> to_bytes;
    if (!Strip0x(to_addr_hex).empty()) {
        const auto dec = HexDecode(to_addr_hex);
        if (dec && dec->size() == 20) to_bytes = *dec;
    }

    const uint64_t recid = static_cast<uint64_t>((compact_sig65[0] - 27) & 3);
    const uint64_t v = recid + 35ULL + 2ULL * chain_id;

    const auto raw_tx = RlpEncodeList({
        RlpEncodeUint64(nonce),
        RlpEncodeUint64(gas_price_wei),
        RlpEncodeUint64(gas_limit),
        RlpEncodeBytes(to_bytes),
        RlpEncodeUint64(value_wei),
        RlpEncodeBytes(data),
        RlpEncodeUint64(v),
        RlpEncodeBigEndianTrimmed(compact_sig65.data() + 1, 32),
        RlpEncodeBigEndianTrimmed(compact_sig65.data() + 33, 32),
    });
    return "0x" + HexEncode(raw_tx);
}

std::string EncodeErc20BalanceOfCalldata(const std::string& owner_addr_hex)
{
    std::string lower = Strip0x(owner_addr_hex);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return "0x70a08231" + Pad64Left(lower);
}

std::string EncodeErc20ApproveCalldata(const std::string& spender_addr_hex, uint64_t amount_units)
{
    std::string lower = Strip0x(spender_addr_hex);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::ostringstream amt_ss;
    amt_ss << std::hex << std::nouppercase << amount_units;
    return "0x095ea7b3" + Pad64Left(lower) + Pad64Left(amt_ss.str());
}

std::vector<uint8_t> BuildP2wpkhScriptPubKey(const std::array<uint8_t, 20>& pubkey_hash20)
{
    std::vector<uint8_t> spk;
    spk.reserve(22);
    spk.push_back(0x00);
    spk.push_back(0x14);
    spk.insert(spk.end(), pubkey_hash20.begin(), pubkey_hash20.end());
    return spk;
}

std::optional<std::vector<uint8_t>> DecodeBech32WitnessProgram(const std::string& addr, std::string& out_hrp, uint8_t& out_witver)
{
    static constexpr char kCharset[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
    const size_t pos = addr.rfind('1');
    if (pos == std::string::npos || pos == 0 || pos + 7 > addr.size()) return std::nullopt;

    out_hrp.clear();
    for (size_t i = 0; i < pos; ++i) {
        out_hrp.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(addr[i]))));
    }

    std::vector<uint8_t> data;
    for (size_t i = pos + 1; i < addr.size(); ++i) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(addr[i])));
        const char* p = std::strchr(kCharset, c);
        if (!p) return std::nullopt;
        data.push_back(static_cast<uint8_t>(p - kCharset));
    }

    std::vector<uint8_t> chk = ExpandHrpLocal(out_hrp);
    chk.insert(chk.end(), data.begin(), data.end());
    const uint32_t pm = Bech32PolymodLocal(chk);
    if (pm != 1 && pm != 0x2bc830a3UL) return std::nullopt;

    out_witver = data[0];
    std::vector<uint8_t> prog;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 1; i + 6 < data.size(); ++i) {
        acc = (acc << 5) | data[i];
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            prog.push_back(static_cast<uint8_t>((acc >> bits) & 0xff));
        }
    }
    if (prog.size() < 2 || prog.size() > 40) return std::nullopt;
    return prog;
}

std::array<uint8_t, 32> ComputeBip143P2wpkhSighash(
    const std::vector<BtcUtxo>& inputs,
    size_t input_index,
    const std::array<uint8_t, 20>& pubkey_hash20,
    const std::vector<std::pair<std::vector<uint8_t>, uint64_t>>& outputs)
{
    std::vector<uint8_t> prevouts;
    std::vector<uint8_t> sequences;
    for (const auto& in : inputs) {
        auto txid_be = HexDecode(in.txid_hex).value_or(std::vector<uint8_t>(32, 0));
        std::reverse(txid_be.begin(), txid_be.end());
        prevouts.insert(prevouts.end(), txid_be.begin(), txid_be.end());
        AppendLE32(prevouts, in.vout);
        AppendLE32(sequences, 0xfffffffdU);
    }
    const auto hash_prevouts = Sha256dLocal(prevouts);
    const auto hash_sequence = Sha256dLocal(sequences);

    std::vector<uint8_t> outs_ser;
    for (const auto& [spk, val] : outputs) {
        AppendLE64(outs_ser, val);
        AppendVarInt(outs_ser, spk.size());
        outs_ser.insert(outs_ser.end(), spk.begin(), spk.end());
    }
    const auto hash_outputs = Sha256dLocal(outs_ser);

    std::vector<uint8_t> preimage;
    AppendLE32(preimage, 2); // nVersion = 2
    preimage.insert(preimage.end(), hash_prevouts.begin(), hash_prevouts.end());
    preimage.insert(preimage.end(), hash_sequence.begin(), hash_sequence.end());

    const auto& cur = inputs[input_index];
    auto cur_txid = HexDecode(cur.txid_hex).value_or(std::vector<uint8_t>(32, 0));
    std::reverse(cur_txid.begin(), cur_txid.end());
    preimage.insert(preimage.end(), cur_txid.begin(), cur_txid.end());
    AppendLE32(preimage, cur.vout);

    // BIP-143 P2WPKH scriptCode: 0x19 76 a9 14 <20-byte-pkh> 88 ac
    preimage.push_back(0x19);
    preimage.push_back(0x76);
    preimage.push_back(0xa9);
    preimage.push_back(0x14);
    preimage.insert(preimage.end(), pubkey_hash20.begin(), pubkey_hash20.end());
    preimage.push_back(0x88);
    preimage.push_back(0xac);

    AppendLE64(preimage, cur.value_sats);
    AppendLE32(preimage, 0xfffffffdU);
    preimage.insert(preimage.end(), hash_outputs.begin(), hash_outputs.end());
    AppendLE32(preimage, 0); // nLockTime = 0
    AppendLE32(preimage, 1); // SIGHASH_ALL = 1

    return Sha256dLocal(preimage);
}

std::string AssembleSignedBtcP2wpkhTxHex(
    const std::vector<BtcUtxo>& inputs,
    const std::vector<std::pair<std::vector<uint8_t>, uint64_t>>& outputs,
    const std::vector<std::vector<uint8_t>>& der_sigs_with_hashtype,
    const std::vector<uint8_t>& compressed_pub33)
{
    if (inputs.empty() || inputs.size() != der_sigs_with_hashtype.size()) return {};
    std::vector<uint8_t> raw;
    AppendLE32(raw, 2);   // version = 2
    raw.push_back(0x00);  // SegWit marker
    raw.push_back(0x01);  // SegWit flag

    AppendVarInt(raw, inputs.size());
    for (const auto& in : inputs) {
        auto txid_le = HexDecode(in.txid_hex).value_or(std::vector<uint8_t>(32, 0));
        std::reverse(txid_le.begin(), txid_le.end());
        raw.insert(raw.end(), txid_le.begin(), txid_le.end());
        AppendLE32(raw, in.vout);
        raw.push_back(0x00); // empty scriptSig for native P2WPKH
        AppendLE32(raw, 0xfffffffdU);
    }

    AppendVarInt(raw, outputs.size());
    for (const auto& [spk, val] : outputs) {
        AppendLE64(raw, val);
        AppendVarInt(raw, spk.size());
        raw.insert(raw.end(), spk.begin(), spk.end());
    }

    for (const auto& sig : der_sigs_with_hashtype) {
        raw.push_back(0x02); // 2 witness items: <sig> <pubkey33>
        AppendVarInt(raw, sig.size());
        raw.insert(raw.end(), sig.begin(), sig.end());
        AppendVarInt(raw, compressed_pub33.size());
        raw.insert(raw.end(), compressed_pub33.begin(), compressed_pub33.end());
    }

    AppendLE32(raw, 0); // nLockTime = 0
    return HexEncode(raw);
}

#if !defined(SCASH_DEX_STANDALONE_TEST)

bool DerivePubkeysFromPriv32(const std::array<uint8_t, 32>& priv32,
                             std::vector<uint8_t>& out_compressed33,
                             std::vector<uint8_t>& out_uncompressed65)
{
    CKey key;
    key.Set(priv32.begin(), priv32.end(), true);
    if (!key.IsValid()) return false;
    CPubKey pub_c = key.GetPubKey();
    if (!pub_c.IsValid() || pub_c.size() != 33) return false;
    out_compressed33.assign(pub_c.begin(), pub_c.end());

    CPubKey pub_u = pub_c;
    if (!pub_u.Decompress() || pub_u.size() != 65) return false;
    out_uncompressed65.assign(pub_u.begin(), pub_u.end());
    return true;
}

bool SignEvmDigestCompact(const std::array<uint8_t, 32>& priv32,
                          const std::array<uint8_t, 32>& digest32,
                          std::vector<uint8_t>& out_compact65)
{
    CKey key;
    key.Set(priv32.begin(), priv32.end(), true);
    if (!key.IsValid()) return false;
    const uint256 hash(std::span<const unsigned char>{digest32.data(), 32});
    return key.SignCompact(hash, out_compact65) && out_compact65.size() == 65;
}

bool SignBtcDigestDer(const std::array<uint8_t, 32>& priv32,
                      const std::array<uint8_t, 32>& digest32,
                      std::vector<uint8_t>& out_der_with_sighash_all)
{
    CKey key;
    key.Set(priv32.begin(), priv32.end(), true);
    if (!key.IsValid()) return false;
    const uint256 hash(std::span<const unsigned char>{digest32.data(), 32});
    if (!key.Sign(hash, out_der_with_sighash_all, true)) return false;
    out_der_with_sighash_all.push_back(0x01); // SIGHASH_ALL
    return true;
}

bool HttpsRequestSync(const std::string& url,
                      const std::string& method,
                      const std::string& content_type,
                      const std::string& body,
                      std::string& out_response,
                      std::string& out_error)
{
#ifdef _WIN32
    const bool is_https = (url.rfind("https://", 0) == 0);
    const size_t scheme_len = is_https ? 8 : (url.rfind("http://", 0) == 0 ? 7 : 0);
    if (scheme_len == 0) {
        out_error = "Unsupported URL scheme";
        return false;
    }
    const std::string rest = url.substr(scheme_len);
    const size_t slash = rest.find('/');
    std::string host_port = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    const std::string path = (slash == std::string::npos) ? "/" : rest.substr(slash);

    INTERNET_PORT port = is_https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    const size_t colon = host_port.find(':');
    if (colon != std::string::npos) {
        port = static_cast<INTERNET_PORT>(std::stoi(host_port.substr(colon + 1)));
        host_port = host_port.substr(0, colon);
    }

    auto to_wide = [](const std::string& s) -> std::wstring {
        if (s.empty()) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
        return w;
    };

    HINTERNET hSession = WinHttpOpen(L"ScashExchangeWallet/0.1",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        out_error = "WinHttpOpen failed";
        return false;
    }
    WinHttpSetTimeouts(hSession, 8000, 8000, 10000, 12000);

    const std::wstring whost = to_wide(host_port);
    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        out_error = "WinHttpConnect failed";
        return false;
    }

    const std::wstring wmethod = to_wide(method);
    const std::wstring wpath = to_wide(path);
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, wmethod.c_str(), wpath.c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            is_https ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        out_error = "WinHttpOpenRequest failed";
        return false;
    }

    std::wstring hdrs = content_type.empty() ? std::wstring() : (L"Content-Type: " + to_wide(content_type) + L"\r\n");
    BOOL ok = WinHttpSendRequest(
        hRequest,
        hdrs.empty() ? static_cast<LPCWSTR>(WINHTTP_NO_ADDITIONAL_HEADERS) : hdrs.c_str(),
        hdrs.empty() ? 0 : static_cast<DWORD>(hdrs.size()),
        body.empty() ? static_cast<LPVOID>(WINHTTP_NO_REQUEST_DATA) : static_cast<LPVOID>(const_cast<char*>(body.data())),
        static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()),
        0);
    if (ok) ok = WinHttpReceiveResponse(hRequest, nullptr);
    if (!ok) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        out_error = "HTTPS network request failed";
        return false;
    }

    out_response.clear();
    DWORD avail = 0;
    do {
        avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
        std::vector<char> chunk(avail);
        DWORD read_bytes = 0;
        if (WinHttpReadData(hRequest, chunk.data(), avail, &read_bytes) && read_bytes > 0) {
            out_response.append(chunk.data(), read_bytes);
        }
    } while (avail > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return true;
#else
    QProcess proc;
    QStringList args;
    args << QStringLiteral("-fsSL")
         << QStringLiteral("--max-time") << QStringLiteral("12")
         << QStringLiteral("-X") << QString::fromStdString(method);
    if (!content_type.empty()) {
        args << QStringLiteral("-H") << QString::fromStdString("Content-Type: " + content_type);
    }
    if (!body.empty()) {
        args << QStringLiteral("--data-binary") << QStringLiteral("@-");
    }
    args << QString::fromStdString(url);

    proc.start(QStringLiteral("curl"), args);
    if (!proc.waitForStarted(3000)) {
        out_error = "Failed to start system curl for HTTPS request";
        return false;
    }
    if (!body.empty()) {
        proc.write(body.data(), static_cast<qint64>(body.size()));
    }
    proc.closeWriteChannel();
    if (!proc.waitForFinished(13000) || proc.exitCode() != 0) {
        out_error = QString::fromUtf8(proc.readAllStandardError()).trimmed().toStdString();
        if (out_error.empty()) out_error = "HTTPS curl request failed";
        return false;
    }
    out_response = QString::fromUtf8(proc.readAllStandardOutput()).toStdString();
    return true;
#endif
}

#endif // !defined(SCASH_DEX_STANDALONE_TEST)

} // namespace scash_dex
