/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "install/verifier.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#include <android-base/logging.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <ziparchive/zip_archive.h>

#include "otautil/print_sha1.h"
#include "private/asn1_decoder.h"

/*
 * Simple version of PKCS#7 SignedData extraction. This extracts the
 * signature OCTET STRING to be used for signature verification.
 *
 * For full details, see http://www.ietf.org/rfc/rfc3852.txt
 *
 * The PKCS#7 structure looks like:
 *
 *   SEQUENCE (ContentInfo)
 *     OID (ContentType)
 *     [0] (content)
 *       SEQUENCE (SignedData)
 *         INTEGER (version CMSVersion)
 *         SET (DigestAlgorithmIdentifiers)
 *         SEQUENCE (EncapsulatedContentInfo)
 *         [0] (CertificateSet OPTIONAL)
 *         [1] (RevocationInfoChoices OPTIONAL)
 *         SET (SignerInfos)
 *           SEQUENCE (SignerInfo)
 *             INTEGER (CMSVersion)
 *             SEQUENCE (SignerIdentifier)
 *             SEQUENCE (DigestAlgorithmIdentifier)
 *             SEQUENCE (SignatureAlgorithmIdentifier)
 *             OCTET STRING (SignatureValue)
 */
static bool read_pkcs7(const uint8_t* pkcs7_der, size_t pkcs7_der_len,
                       std::vector<uint8_t>* sig_der) {
  CHECK(sig_der != nullptr);
  sig_der->clear();

  asn1_context ctx(pkcs7_der, pkcs7_der_len);

  std::unique_ptr<asn1_context> pkcs7_seq(ctx.asn1_sequence_get());
  if (pkcs7_seq == nullptr || !pkcs7_seq->asn1_sequence_next()) {
    return false;
  }

  std::unique_ptr<asn1_context> signed_data_app(pkcs7_seq->asn1_constructed_get());
  if (signed_data_app == nullptr) {
    return false;
  }

  std::unique_ptr<asn1_context> signed_data_seq(signed_data_app->asn1_sequence_get());
  if (signed_data_seq == nullptr || !signed_data_seq->asn1_sequence_next() ||
      !signed_data_seq->asn1_sequence_next() || !signed_data_seq->asn1_sequence_next() ||
      !signed_data_seq->asn1_constructed_skip_all()) {
    return false;
  }

  std::unique_ptr<asn1_context> sig_set(signed_data_seq->asn1_set_get());
  if (sig_set == nullptr) {
    return false;
  }

  std::unique_ptr<asn1_context> sig_seq(sig_set->asn1_sequence_get());
  if (sig_seq == nullptr || !sig_seq->asn1_sequence_next() || !sig_seq->asn1_sequence_next() ||
      !sig_seq->asn1_sequence_next() || !sig_seq->asn1_sequence_next()) {
    return false;
  }

  const uint8_t* sig_der_ptr;
  size_t sig_der_length;
  if (!sig_seq->asn1_octet_string_get(&sig_der_ptr, &sig_der_length)) {
    return false;
  }

  sig_der->resize(sig_der_length);
  std::copy(sig_der_ptr, sig_der_ptr + sig_der_length, sig_der->begin());
  return true;
}

int verify_file(VerifierInterface* package, const std::vector<Certificate>& keys) {
  CHECK(package);
  package->SetProgress(0.0);

  // An archive with a whole-file signature will end in six bytes:
  //
  //   (2-byte signature start) $ff $ff (2-byte comment size)
  //
  // (As far as the ZIP format is concerned, these are part of the archive comment.) We start by
  // reading this footer, this tells us how far back from the end we have to start reading to find
  // the whole comment.

#define FOOTER_SIZE 6
  uint64_t length = package->GetPackageSize();

  if (length < FOOTER_SIZE) {
    LOG(ERROR) << "not big enough to contain footer";
    return VERIFY_FAILURE;
  }

  uint8_t footer[FOOTER_SIZE];
  if (!package->ReadFullyAtOffset(footer, FOOTER_SIZE, length - FOOTER_SIZE)) {
    LOG(ERROR) << "Failed to read footer";
    return VERIFY_FAILURE;
  }

  if (footer[2] != 0xff || footer[3] != 0xff) {
    LOG(ERROR) << "footer is wrong";
    return VERIFY_FAILURE;
  }

  size_t comment_size = footer[4] + (footer[5] << 8);
  size_t signature_start = footer[0] + (footer[1] << 8);
  LOG(INFO) << "comment is " << comment_size << " bytes; signature is " << signature_start
            << " bytes from end";

  if (signature_start > comment_size) {
    LOG(ERROR) << "signature start: " << signature_start
               << " is larger than comment size: " << comment_size;
    return VERIFY_FAILURE;
  }

  if (signature_start <= FOOTER_SIZE) {
    LOG(ERROR) << "Signature start is in the footer";
    return VERIFY_FAILURE;
  }

#define EOCD_HEADER_SIZE 22

  // The end-of-central-directory record is 22 bytes plus any comment length.
  size_t eocd_size = comment_size + EOCD_HEADER_SIZE;

  if (length < eocd_size) {
    LOG(ERROR) << "not big enough to contain EOCD";
    return VERIFY_FAILURE;
  }

  // Determine how much of the file is covered by the signature. This is everything except the
  // signature data and length, which includes all of the EOCD except for the comment length field
  // (2 bytes) and the comment data.
  uint64_t signed_len = length - eocd_size + EOCD_HEADER_SIZE - 2;

  uint8_t eocd[eocd_size];
  if (!package->ReadFullyAtOffset(eocd, eocd_size, length - eocd_size)) {
    LOG(ERROR) << "Failed to read EOCD of " << eocd_size << " bytes";
    return VERIFY_FAILURE;
  }

  // If this is really is the EOCD record, it will begin with the magic number $50 $4b $05 $06.
  if (eocd[0] != 0x50 || eocd[1] != 0x4b || eocd[2] != 0x05 || eocd[3] != 0x06) {
    LOG(ERROR) << "signature length doesn't match EOCD marker";
    return VERIFY_FAILURE;
  }

  for (size_t i = 4; i < eocd_size - 3; ++i) {
    if (eocd[i] == 0x50 && eocd[i + 1] == 0x4b && eocd[i + 2] == 0x05 && eocd[i + 3] == 0x06) {
      // If the sequence $50 $4b $05 $06 appears anywhere after the real one, libziparchive will
      // find the later (wrong) one, which could be exploitable. Fail the verification if this
      // sequence occurs anywhere after the real one.
      LOG(ERROR) << "EOCD marker occurs after start of EOCD";
      return VERIFY_FAILURE;
    }
  }

  bool need_sha1 = false;
  bool need_sha256 = false;
  for (const auto& key : keys) {
    switch (key.hash_len) {
      case SHA_DIGEST_LENGTH:
        need_sha1 = true;
        break;
      case SHA256_DIGEST_LENGTH:
        need_sha256 = true;
        break;
    }
  }

  SHA_CTX sha1_ctx;
  SHA256_CTX sha256_ctx;
  SHA1_Init(&sha1_ctx);
  SHA256_Init(&sha256_ctx);

  std::vector<HasherUpdateCallback> hashers;
  if (need_sha1) {
    hashers.emplace_back(
        std::bind(&SHA1_Update, &sha1_ctx, std::placeholders::_1, std::placeholders::_2));
  }
  if (need_sha256) {
    hashers.emplace_back(
        std::bind(&SHA256_Update, &sha256_ctx, std::placeholders::_1, std::placeholders::_2));
  }

  double frac = -1.0;
  uint64_t so_far = 0;
  while (so_far < signed_len) {
    // On a Nexus 5X, experiment showed 16MiB beat 1MiB by 6% faster for a 1196MiB full OTA and
    // 60% for an 89MiB incremental OTA. http://b/28135231.
    uint64_t read_size = std::min<uint64_t>(signed_len - so_far, 16 * MiB);
    package->UpdateHashAtOffset(hashers, so_far, read_size);
    so_far += read_size;

    double f = so_far / static_cast<double>(signed_len);
    if (f > frac + 0.02 || read_size == so_far) {
      package->SetProgress(f);
      frac = f;
    }
  }

  uint8_t sha1[SHA_DIGEST_LENGTH];
  SHA1_Final(sha1, &sha1_ctx);
  uint8_t sha256[SHA256_DIGEST_LENGTH];
  SHA256_Final(sha256, &sha256_ctx);

  const uint8_t* signature = eocd + eocd_size - signature_start;
  size_t signature_size = signature_start - FOOTER_SIZE;

  LOG(INFO) << "signature (offset: " << std::hex << (length - signature_start)
            << ", length: " << signature_size << "): " << print_hex(signature, signature_size);

  std::vector<uint8_t> sig_der;
  if (!read_pkcs7(signature, signature_size, &sig_der)) {
    LOG(ERROR) << "Could not find signature DER block";
    return VERIFY_FAILURE;
  }

  // Check to make sure at least one of the keys matches the signature. Since any key can match,
  // we need to try each before determining a verification failure has happened.
  size_t i = 0;
  for (const auto& key : keys) {
    const uint8_t* hash;
    int hash_nid;
    switch (key.hash_len) {
      case SHA_DIGEST_LENGTH:
        hash = sha1;
        hash_nid = NID_sha1;
        break;
      case SHA256_DIGEST_LENGTH:
        hash = sha256;
        hash_nid = NID_sha256;
        break;
      default:
        continue;
    }

    // The 6 bytes is the "(signature_start) $ff $ff (comment_size)" that the signing tool appends
    // after the signature itself.
    if (key.key_type == Certificate::KEY_TYPE_RSA) {
      if (!RSA_verify(hash_nid, hash, key.hash_len, sig_der.data(), sig_der.size(),
                      key.rsa.get())) {
        LOG(INFO) << "failed to verify against RSA key " << i;
        continue;
      }

      LOG(INFO) << "whole-file signature verified against RSA key " << i;
      return VERIFY_SUCCESS;
    } else if (key.key_type == Certificate::KEY_TYPE_EC && key.hash_len == SHA256_DIGEST_LENGTH) {
      if (!ECDSA_verify(0, hash, key.hash_len, sig_der.data(), sig_der.size(), key.ec.get())) {
        LOG(INFO) << "failed to verify against EC key " << i;
        continue;
      }

      LOG(INFO) << "whole-file signature verified against EC key " << i;
      return VERIFY_SUCCESS;
    } else {
      LOG(INFO) << "Unknown key type " << key.key_type;
    }
    i++;
  }

  if (need_sha1) {
    LOG(INFO) << "SHA-1 digest: " << print_hex(sha1, SHA_DIGEST_LENGTH);
  }
  if (need_sha256) {
    LOG(INFO) << "SHA-256 digest: " << print_hex(sha256, SHA256_DIGEST_LENGTH);
  }
  LOG(ERROR) << "failed to verify whole-file signature";
  return VERIFY_FAILURE;
}

static std::vector<Certificate> IterateZipEntriesAndSearchForKeys(const ZipArchiveHandle& handle) {
  void* cookie;
  int32_t iter_status = StartIteration(handle, &cookie, "", "x509.pem");
  if (iter_status != 0) {
    LOG(ERROR) << "Failed to iterate over entries in the certificate zipfile: "
               << ErrorCodeString(iter_status);
    return {};
  }

  std::vector<Certificate> result;

  std::string_view name;
  ZipEntry entry;
  while ((iter_status = Next(cookie, &entry, &name)) == 0) {
    std::vector<uint8_t> pem_content(entry.uncompressed_length);
    if (int32_t extract_status =
            ExtractToMemory(handle, &entry, pem_content.data(), pem_content.size());
        extract_status != 0) {
      LOG(ERROR) << "Failed to extract " << name;
      return {};
    }

    Certificate cert(0, Certificate::KEY_TYPE_RSA, nullptr, nullptr);
    // Aborts the parsing if we fail to load one of the key file.
    if (!LoadCertificateFromBuffer(pem_content, &cert)) {
      LOG(ERROR) << "Failed to load keys from " << name;
      return {};
    }

    result.emplace_back(std::move(cert));
  }

  if (iter_status != -1) {
    LOG(ERROR) << "Error while iterating over zip entries: " << ErrorCodeString(iter_status);
    return {};
  }

  return result;
}

std::vector<Certificate> LoadKeysFromZipfile(const std::string& zip_name) {
  ZipArchiveHandle handle;
  if (int32_t open_status = OpenArchive(zip_name.c_str(), &handle); open_status != 0) {
    LOG(ERROR) << "Failed to open " << zip_name << ": " << ErrorCodeString(open_status);
    return {};
  }

  std::vector<Certificate> result = IterateZipEntriesAndSearchForKeys(handle);
  CloseArchive(handle);
  return result;
}

bool CheckRSAKey(const std::unique_ptr<RSA, RSADeleter>& rsa) {
  if (!rsa) {
    return false;
  }

  const BIGNUM* out_n;
  const BIGNUM* out_e;
  RSA_get0_key(rsa.get(), &out_n, &out_e, nullptr /* private exponent */);
  auto modulus_bits = BN_num_bits(out_n);
  if (modulus_bits != 2048 && modulus_bits != 4096) {
    LOG(ERROR) << "Modulus should be 2048 or 4096 bits long, actual: " << modulus_bits;
    return false;
  }

  BN_ULONG exponent = BN_get_word(out_e);
  if (exponent != 3 && exponent != 65537) {
    LOG(ERROR) << "Public exponent should be 3 or 65537, actual: " << exponent;
    return false;
  }

  return true;
}

bool CheckECKey(const std::unique_ptr<EC_KEY, ECKEYDeleter>& ec_key) {
  if (!ec_key) {
    return false;
  }

  const EC_GROUP* ec_group = EC_KEY_get0_group(ec_key.get());
  if (!ec_group) {
    LOG(ERROR) << "Failed to get the ec_group from the ec_key";
    return false;
  }
  auto degree = EC_GROUP_get_degree(ec_group);
  if (degree != 256) {
    LOG(ERROR) << "Field size of the ec key should be 256 bits long, actual: " << degree;
    return false;
  }

  return true;
}

bool LoadCertificateFromBuffer(const std::vector<uint8_t>& pem_content, Certificate* cert) {
  std::unique_ptr<BIO, decltype(&BIO_free)> content(
      BIO_new_mem_buf(pem_content.data(), pem_content.size()), BIO_free);

  std::unique_ptr<X509, decltype(&X509_free)> x509(
      PEM_read_bio_X509(content.get(), nullptr, nullptr, nullptr), X509_free);
  if (!x509) {
    LOG(ERROR) << "Failed to read x509 certificate";
    return false;
  }

  int nid = X509_get_signature_nid(x509.get());
  switch (nid) {
    // SignApk has historically accepted md5WithRSA certificates, but treated them as
    // sha1WithRSA anyway. Continue to do so for backwards compatibility.
    case NID_md5WithRSA:
    case NID_md5WithRSAEncryption:
    case NID_sha1WithRSA:
    case NID_sha1WithRSAEncryption:
      cert->hash_len = SHA_DIGEST_LENGTH;
      break;
    case NID_sha256WithRSAEncryption:
    case NID_ecdsa_with_SHA256:
      cert->hash_len = SHA256_DIGEST_LENGTH;
      break;
    default:
      LOG(ERROR) << "Unrecognized signature nid " << OBJ_nid2ln(nid);
      return false;
  }

  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> public_key(X509_get_pubkey(x509.get()),
                                                                 EVP_PKEY_free);
  if (!public_key) {
    LOG(ERROR) << "Failed to extract the public key from x509 certificate";
    return false;
  }

  int key_type = EVP_PKEY_id(public_key.get());
  if (key_type == EVP_PKEY_RSA) {
    cert->key_type = Certificate::KEY_TYPE_RSA;
    cert->ec.reset();
    cert->rsa.reset(EVP_PKEY_get1_RSA(public_key.get()));
    if (!cert->rsa || !CheckRSAKey(cert->rsa)) {
      LOG(ERROR) << "Failed to validate the rsa key info from public key";
      return false;
    }
  } else if (key_type == EVP_PKEY_EC) {
    cert->key_type = Certificate::KEY_TYPE_EC;
    cert->rsa.reset();
    cert->ec.reset(EVP_PKEY_get1_EC_KEY(public_key.get()));
    if (!cert->ec || !CheckECKey(cert->ec)) {
      LOG(ERROR) << "Failed to validate the ec key info from the public key";
      return false;
    }
  } else {
    LOG(ERROR) << "Unrecognized public key type " << OBJ_nid2ln(key_type);
    return false;
  }

  return true;
}
