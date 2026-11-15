// Copyright (c) 2026 The Scash Network Community developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_EXCHANGEMULTICOIN_H
#define BITCOIN_QT_EXCHANGEMULTICOIN_H

#include "exchangehtlc.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace scash_dex {

struct BtcUtxo {
    std::string txid_hex;
    uint32_t vout{0};
    uint64_t value_sats{0};
};

// Official Ethereum Keccak-256 (0x01 padding byte — distinct from NIST SHA3-256)
std::array<uint8_t, 32> Keccak256Bytes(const uint8_t* data, size_t len);
std::array<uint8_t, 32> Keccak256Bytes(const std::vector<uint8_t>& data);

// Derives EIP-55 checksummed `0x...` address from a 65-byte uncompressed secp256k1 pubkey (`0x04 || X || Y`)
std::string DeriveEvmAddressFromUncompressedPubkey(const std::vector<uint8_t>& uncompressed_pub65);

// Derives Bitcoin mainnet SegWit v0 P2WPKH (`bc1q...`, 42 chars) from a 33-byte compressed secp256k1 pubkey
std::string DeriveBtcP2wpkhAddress(const std::vector<uint8_t>& compressed_pub33, const std::string& hrp = "bc");

// Encodes a 32-byte private key as compressed Bitcoin Mainnet Base58Check WIF (`K...` / `L...`)
std::string EncodeBitcoinWifCompressed(const std::array<uint8_t, 32>& privkey32);

// Canonical Ethereum RLP primitives & EIP-155 raw transaction encoding
std::vector<uint8_t> RlpEncodeUint64(uint64_t val);
std::vector<uint8_t> RlpEncodeBigEndianTrimmed(const uint8_t* bytes, size_t len);
std::vector<uint8_t> RlpEncodeBytes(const std::vector<uint8_t>& data);
std::vector<uint8_t> RlpEncodeList(const std::vector<std::vector<uint8_t>>& items);

// Builds the EIP-155 signing preimage RLP `[nonce, gasPrice, gasLimit, to, value, data, chainId, 0, 0]`
// and returns its 32-byte Keccak-256 sighash
std::array<uint8_t, 32> ComputeEip155SigningHash(
    uint64_t nonce,
    uint64_t gas_price_wei,
    uint64_t gas_limit,
    const std::string& to_addr_hex,
    uint64_t value_wei,
    const std::vector<uint8_t>& data,
    uint64_t chain_id);

// Packs a 65-byte Bitcoin Core `SignCompact` signature (`header, r[32], s[32]`) into a broadcast-ready
// `0x...` EIP-155 signed raw transaction hex string
std::string AssembleSignedEip155TxHex(
    uint64_t nonce,
    uint64_t gas_price_wei,
    uint64_t gas_limit,
    const std::string& to_addr_hex,
    uint64_t value_wei,
    const std::vector<uint8_t>& data,
    uint64_t chain_id,
    const std::vector<uint8_t>& compact_sig65);

// ERC-20 calldata builders (`balanceOf`, `allowance`, `approve`)
std::string EncodeErc20BalanceOfCalldata(const std::string& owner_addr_hex);
std::string EncodeErc20ApproveCalldata(const std::string& spender_addr_hex, uint64_t amount_units);

// BIP-143 SegWit v0 P2WPKH sighash + raw signed Bitcoin SegWit transaction builder
std::array<uint8_t, 32> ComputeBip143P2wpkhSighash(
    const std::vector<BtcUtxo>& inputs,
    size_t input_index,
    const std::array<uint8_t, 20>& pubkey_hash20,
    const std::vector<std::pair<std::vector<uint8_t>, uint64_t>>& outputs);

std::string AssembleSignedBtcP2wpkhTxHex(
    const std::vector<BtcUtxo>& inputs,
    const std::vector<std::pair<std::vector<uint8_t>, uint64_t>>& outputs,
    const std::vector<std::vector<uint8_t>>& der_sigs_with_hashtype,
    const std::vector<uint8_t>& compressed_pub33);

std::vector<uint8_t> BuildP2wpkhScriptPubKey(const std::array<uint8_t, 20>& pubkey_hash20);
std::optional<std::vector<uint8_t>> DecodeBech32WitnessProgram(const std::string& addr, std::string& out_hrp, uint8_t& out_witver);

#if !defined(SCASH_DEX_STANDALONE_TEST)
// Native HTTPS GET / POST client (WinHTTP TLS on Windows 11, system curl TLS on Linux)
bool HttpsRequestSync(const std::string& url,
                      const std::string& method,
                      const std::string& content_type,
                      const std::string& body,
                      std::string& out_response,
                      std::string& out_error);

// Derives 65-byte uncompressed & 33-byte compressed pubkeys and signs digests using Scash Core's libsecp256k1 `CKey`
bool DerivePubkeysFromPriv32(const std::array<uint8_t, 32>& priv32,
                             std::vector<uint8_t>& out_compressed33,
                             std::vector<uint8_t>& out_uncompressed65);
bool SignEvmDigestCompact(const std::array<uint8_t, 32>& priv32,
                          const std::array<uint8_t, 32>& digest32,
                          std::vector<uint8_t>& out_compact65);
bool SignBtcDigestDer(const std::array<uint8_t, 32>& priv32,
                      const std::array<uint8_t, 32>& digest32,
                      std::vector<uint8_t>& out_der_with_sighash_all);
#endif

} // namespace scash_dex

#endif // BITCOIN_QT_EXCHANGEMULTICOIN_H
