#include <string.h>
#include "edhoc/edhoc.h"

#include "cipher_suites.h"
#include "crypto.h"
#include "format.h"
#include "cbor.h"

#include "process.h"
#include "credentials.h"

ssize_t edhoc_create_msg1(edhoc_ctx_t *ctx, corr_t corr, method_t m, cipher_suite_id_t id, uint8_t *out, size_t olen) {
    ssize_t ret;

    ssize_t msg1_len;

    const cipher_suite_t *suite_info;

    if (ctx->state != EDHOC_WAITING) {
        ctx->state = EDHOC_FAILED;
        EDHOC_FAIL(EDHOC_ERR_ILLEGAL_STATE);
    }

    if ((suite_info = edhoc_cipher_suite_from_id(id)) != NULL) {
        ctx->session.cipher_suite = suite_info->id;
    } else {
        EDHOC_FAIL(EDHOC_ERR_CIPHERSUITE_UNAVAILABLE);
    }

    ctx->correlation = corr;
    ctx->method = m;

    // if not already initialized, generate and load ephemeral key
    if (ctx->local_eph_key.kty == COSE_KTY_NONE) {
        EDHOC_CHECK_SUCCESS(crypt_gen_keypair(suite_info->dh_curve, &ctx->local_eph_key));
    }

    if ((msg1_len = edhoc_msg1_encode(ctx->correlation,
                                      ctx->method,
                                      ctx->session.cipher_suite,
                                      &ctx->local_eph_key,
                                      ctx->session.cidi,
                                      ctx->session.cidi_len,
                                      ctx->conf->ad1,
                                      out,
                                      olen)) <= 0) {
        if (msg1_len < 0) {
            EDHOC_FAIL(msg1_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_SIZE);
        }
    }

    if (ctx->state == EDHOC_WAITING) {
        ctx->state = EDHOC_SENT_MESSAGE_1;
    } else {
        ctx->state = EDHOC_FAILED;
        EDHOC_FAIL(EDHOC_ERR_ILLEGAL_STATE);
    }

    ret = msg1_len;
    exit:
    return ret;
}

ssize_t edhoc_compute_mac23(cose_algo_t aead,
                            const cred_container_t *local_cred,
                            const uint8_t *k_23m,
                            const uint8_t *iv_23m,
                            const uint8_t *th23,
                            uint8_t *out) {
    ssize_t ret;
    ssize_t a23m_len;
    ssize_t cred_len;

    const aead_info_t *aead_info;

    const uint8_t *cred;

    uint8_t a23m_buf[EDHOC_A23M_MAX_SIZE];

    if ((aead_info = cose_aead_info_from_id(aead)) == NULL) {
        EDHOC_FAIL(EDHOC_ERR_CIPHERSUITE_UNAVAILABLE);
    }

    if ((cred_len = cred_get_cred_bytes(local_cred, &cred)) <= 0) {
        if (cred_len < 0) {
            EDHOC_FAIL(cred_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_CRED);
        }
    }

    if ((a23m_len = edhoc_a23m_encode(cred,
                                      cred_len,
                                      local_cred->cred_id,
                                      local_cred->cred_id_len,
                                      th23,
                                      a23m_buf,
                                      EDHOC_A23M_MAX_SIZE)) <= 0) {
        if (a23m_len < 0) {
            EDHOC_FAIL(a23m_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_SIZE);
        }
    }

    EDHOC_CHECK_SUCCESS(crypt_encrypt(aead_info->id,        // COSE algorithm ID
                                      k_23m,                // encryption key
                                      iv_23m,               // nonce
                                      a23m_buf,             // aad
                                      a23m_len,             // aad len
                                      out,                  // plaintext
                                      out,                  // ciphertext
                                      0,                    // length of plaintext and ciphertext
                                      out));                // pointer to tag (size depends on selected algorithm)
    ret = aead_info->tag_length;
    exit:
    return ret;
}

ssize_t edhoc_compute_sig23(edhoc_role_t role,
                            method_t method,
                            const cred_container_t *local_cred,
                            const uint8_t *tag,
                            size_t tag_len,
                            const uint8_t *th23,
                            ad_cb_t ad23,
                            uint8_t *out) {
    ssize_t ret;

    ssize_t cred_len;
    ssize_t m23_len;
    ssize_t sig_len;

    const uint8_t *cred;

    uint8_t m23_buf[EDHOC_M23_MAX_SIZE];

    if ((cred_len = cred_get_cred_bytes(local_cred, &cred)) <= 0) {
        if (cred_len < 0) {
            EDHOC_FAIL(cred_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_CRED);
        }
    }

    // here we start reusing the m23_or_a23m buffer
    if (role == EDHOC_IS_RESPONDER) {

        // perform signature-based authentication
        if (method == EDHOC_AUTH_SIGN_SIGN || method == EDHOC_AUTH_STATIC_SIGN) {
            if ((m23_len = edhoc_m23_encode(th23,
                                            cred,
                                            cred_len,
                                            local_cred->cred_id,
                                            local_cred->cred_id_len,
                                            ad23,
                                            tag,
                                            tag_len,
                                            m23_buf,
                                            EDHOC_M23_MAX_SIZE)) <= EDHOC_SUCCESS) {
                EDHOC_FAIL(m23_len);
            }

            // compute signature
            if ((sig_len = crypt_sign(&local_cred->auth_key, m23_buf, m23_len, out)) <= 0) {
                if (sig_len < 0) {
                    EDHOC_FAIL(sig_len);
                } else {
                    EDHOC_FAIL(EDHOC_ERR_INVALID_SIZE);
                }

            }

            ret = sig_len;
        } else {
            ret = tag_len;
        }
    } else {

        // perform signature-based authentication
        if (method == EDHOC_AUTH_SIGN_SIGN || method == EDHOC_AUTH_SIGN_STATIC) {
            if ((m23_len = edhoc_m23_encode(th23,
                                            cred,
                                            cred_len,
                                            local_cred->cred_id,
                                            local_cred->cred_id_len,
                                            ad23,
                                            tag,
                                            tag_len,
                                            m23_buf,
                                            EDHOC_M23_MAX_SIZE)) <= 0) {
                EDHOC_FAIL(m23_len);
            }

            // compute signature
            if ((sig_len = crypt_sign(&local_cred->auth_key, m23_buf, m23_len, out)) <= 0) {
                if (sig_len < 0) {
                    EDHOC_FAIL(sig_len);
                } else {
                    EDHOC_FAIL(EDHOC_ERR_INVALID_SIZE);
                }
            }

            ret = sig_len;
        } else {
            ret = tag_len;
        }
    }

    exit:
    return ret;
}

ssize_t edhoc_create_ciphertext3(cose_algo_t id,
                                 method_t m,
                                 const uint8_t *prk4x3m,
                                 const uint8_t *th3,
                                 const cred_container_t *local_cred,
                                 ad_cb_t ad3,
                                 uint8_t *out,
                                 size_t olen) {
    ssize_t ret;
    ssize_t size;

    const aead_info_t *aead_info = NULL;

    uint8_t k3m_or_k3ae[EDHOC_K23M_MAX_SIZE];
    size_t k3m_len, k3ae_len;

    uint8_t cred_id_buf[EDHOC_CREDENTIAL_ID_MAX_SIZE];
    ssize_t cred_id_len;

    uint8_t iv3m_or_iv3ae[EDHOC_IV23M_MAX_SIZE];
    size_t iv3m_len, iv3ae_len;

    uint8_t a_3ae[EDHOC_MAX_A3AE_LEN];
    ssize_t a3ae_len;

    uint8_t sig_or_mac3[EDHOC_SIG23_MAX_SIZE];
    ssize_t sig_or_mac3_len;

    uint8_t tag[EDHOC_AUTH_TAG_MAX_SIZE];
    ssize_t tag_len;

    if ((aead_info = cose_aead_info_from_id(id)) == NULL) {
        EDHOC_FAIL(EDHOC_ERR_AEAD_CIPHER_UNAVAILABLE);
    }

    k3m_len = k3ae_len = aead_info->key_length;
    iv3m_len = iv3ae_len = aead_info->iv_length;
    tag_len = aead_info->tag_length;

    EDHOC_CHECK_SUCCESS(crypt_kdf(id, prk4x3m, th3, "K_3m", k3m_len, k3m_or_k3ae));
    EDHOC_CHECK_SUCCESS(crypt_kdf(id, prk4x3m, th3, "IV_3m", iv3m_len, iv3m_or_iv3ae));

    if ((sig_or_mac3_len = edhoc_compute_mac23(id, local_cred, k3m_or_k3ae, iv3m_or_iv3ae, th3, sig_or_mac3)) <= 0) {
        if (sig_or_mac3_len < 0) {
            EDHOC_FAIL(sig_or_mac3_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_SIZE);
        }
    }

#if defined(EDHOC_AUTH_METHOD_0_ENABLED) || defined(EDHOC_AUTH_METHOD_1_ENABLED)
    if ((sig_or_mac3_len = edhoc_compute_sig23(EDHOC_IS_INITIATOR,
                                               m,
                                               local_cred,
                                               sig_or_mac3,
                                               sig_or_mac3_len,
                                               th3,
                                               ad3,
                                               sig_or_mac3)) <= 0) {
        if (sig_or_mac3_len < 0) {
            EDHOC_FAIL(sig_or_mac3_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_SIZE);
        }
    }
#endif

    if ((cred_id_len = cred_get_cred_id_bytes(local_cred, cred_id_buf, sizeof(cred_id_buf))) <= 0) {
        if (cred_id_len < 0) {
            EDHOC_FAIL(cred_id_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_CRED_ID);
        }
    }

    // compute P_3ae and write it to the output buffer
    if ((size = edhoc_p2e_or_p3ae_encode(cred_id_buf, cred_id_len, sig_or_mac3, sig_or_mac3_len, out, olen)) <= 0) {
        if (size < 0) {
            EDHOC_FAIL(size);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_SIZE);
        }
    }

    if ((a3ae_len = edhoc_a3ae_encode(th3, a_3ae, EDHOC_MAX_A3AE_LEN)) <= 0) {
        if (a3ae_len < 0) {
            EDHOC_FAIL(a3ae_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_SIZE);
        }
    }

    EDHOC_CHECK_SUCCESS(crypt_kdf(id, prk4x3m, th3, "K_3ae", k3ae_len, k3m_or_k3ae));
    EDHOC_CHECK_SUCCESS(crypt_kdf(id, prk4x3m, th3, "IV_3ae", iv3ae_len, iv3m_or_iv3ae));

    EDHOC_CHECK_SUCCESS(crypt_encrypt(id,
                                      k3m_or_k3ae,
                                      iv3m_or_iv3ae,
                                      a_3ae,
                                      a3ae_len,
                                      out,
                                      out,
                                      size,
                                      tag));

    // copy the tag
    if (tag_len + size > (ssize_t) olen) {
        EDHOC_FAIL(EDHOC_ERR_BUFFER_OVERFLOW);
    } else {
        memcpy(out + size, tag, tag_len);
        size += tag_len;
    }

    ret = size;
    exit:
    return ret;
}

ssize_t edhoc_create_ciphertext2(cose_algo_t id,
                                 const uint8_t *prk2e,
                                 const uint8_t *th2,
                                 const uint8_t* plaintext,
                                 size_t pt2_size,
                                 uint8_t *out) {
    ssize_t ret;
    uint8_t k2e[EDHOC_PAYLOAD_MAX_SIZE];

    EDHOC_CHECK_SUCCESS(crypt_kdf(id, prk2e, th2, "K_2e", pt2_size, k2e));

    // XOR encryption P_2e XOR K_2e
    for (size_t i = 0; i < pt2_size; i++) {
        out[i] = plaintext[i] ^ k2e[i];
    }

    ret = pt2_size;
    exit:
    return ret;
}


ssize_t edhoc_create_plaintext2(cose_algo_t id,
                                method_t m,
                                const uint8_t *prk3e2m,
                                const uint8_t *th2,
                                const cred_container_t *local_cred,
                                ad_cb_t ad2,
                                uint8_t *out,
                                size_t olen) {
    ssize_t ret;
    const aead_info_t *aead_info = NULL;

    const uint8_t *cred;
    ssize_t cred_len;
    ssize_t size;

    // temporary buffers
    uint8_t cred_id[EDHOC_CREDENTIAL_ID_MAX_SIZE];
    ssize_t cred_id_len;

    uint8_t sig_or_mac2[EDHOC_SIG23_MAX_SIZE];
    ssize_t sig_or_mac2_len;

    uint8_t k2m[EDHOC_K23M_MAX_SIZE];
    ssize_t k2m_len;

    uint8_t iv2m[EDHOC_IV23M_MAX_SIZE];
    ssize_t iv2m_len;

    if ((aead_info = cose_aead_info_from_id(id)) != NULL) {
        k2m_len = aead_info->key_length;
        iv2m_len = aead_info->iv_length;
    } else {
        EDHOC_FAIL(EDHOC_ERR_AEAD_CIPHER_UNAVAILABLE);
    }

    EDHOC_CHECK_SUCCESS(crypt_kdf(id, prk3e2m, th2, "K_2m", k2m_len, k2m));
    EDHOC_CHECK_SUCCESS(crypt_kdf(id, prk3e2m, th2, "IV_2m", iv2m_len, iv2m));

    if ((cred_len = cred_get_cred_bytes(local_cred, &cred)) <= 0) {
        if (cred_len < 0) {
            EDHOC_FAIL(cred_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_CRED);
        }
    }

    if ((sig_or_mac2_len = edhoc_compute_mac23(id, local_cred, k2m, iv2m, th2, sig_or_mac2)) <= 0) {
        if (sig_or_mac2_len < 0) {
            EDHOC_FAIL(sig_or_mac2_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_SIZE);
        }
    }

#if defined(EDHOC_AUTH_METHOD_0_ENABLED) || defined(EDHOC_AUTH_METHOD_2_ENABLED)
    if ((sig_or_mac2_len = edhoc_compute_sig23(EDHOC_IS_RESPONDER,
                                               m,
                                               local_cred,
                                               sig_or_mac2,
                                               sig_or_mac2_len,
                                               th2,
                                               ad2,
                                               sig_or_mac2)) <= 0) {
        if (sig_or_mac2_len < 0) {
            EDHOC_FAIL(sig_or_mac2_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_SIZE);
        }
    }
#endif

    if ((cred_id_len = cred_get_cred_id_bytes(local_cred, cred_id, sizeof(cred_id))) <= 0) {
        if (cred_id_len < 0) {
            EDHOC_FAIL(cred_id_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_SIZE);
        }
    }

    // compute P_2e and write it to the output buffer
    if ((size = edhoc_p2e_or_p3ae_encode(cred_id, cred_id_len, sig_or_mac2, sig_or_mac2_len, out, olen)) <= 0) {
        if (size < 0) {
            EDHOC_FAIL(size);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_SIZE);
        }
    }

    ret = size;
    exit:
    return ret;
}

ssize_t edhoc_create_msg3(edhoc_ctx_t *ctx, const uint8_t *msg2_buf, size_t msg2_len, uint8_t *out, size_t olen) {
    ssize_t ret;
    ssize_t data3_len, msg1_len, msg3_len;

    edhoc_msg2_t msg2;

    const cipher_suite_t *suite_info;

    uint8_t ciphertext_3[EDHOC_PAYLOAD_MAX_SIZE];
    ssize_t ct3_len;

    uint8_t k2e_buf[EDHOC_PAYLOAD_MAX_SIZE];

    memset(&msg2, 0, sizeof(edhoc_msg2_t));

    if (ctx->state == EDHOC_SENT_MESSAGE_1) {
        ctx->state = EDHOC_RECEIVED_MESSAGE_2;
    } else {
        ctx->state = EDHOC_FAILED;
        EDHOC_FAIL(EDHOC_ERR_ILLEGAL_STATE);
    }

    if ((suite_info = edhoc_cipher_suite_from_id(ctx->session.cipher_suite)) == NULL) {
        EDHOC_FAIL(EDHOC_ERR_CIPHERSUITE_UNAVAILABLE);
    }

    EDHOC_CHECK_SUCCESS(edhoc_msg2_decode(&msg2, ctx->correlation, msg2_buf, msg2_len));

    // Check the Initiator's connection identifier
    if (msg2.cidi_len > 0 && msg2.cidi_len <= EDHOC_CID_MAX_LEN) {
        // TODO: copy Initiator connection identifier into temporary buffer and verify that it is known.
    } else if (msg2.cidi_len > EDHOC_CID_MAX_LEN) {
        EDHOC_FAIL(EDHOC_ERR_BUFFER_OVERFLOW);
    }

    if (msg2.g_y_len <= EDHOC_ECC_KEY_MAX_SIZE && msg2.g_y_len > 0) {
        if (msg2.g_y != NULL) {
            memcpy(ctx->remote_eph_key.x, msg2.g_y, msg2.g_y_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_PARAM);
        }

        // TODO: verify if valid public key
        ctx->remote_eph_key.x_len = msg2.g_y_len;

        ctx->remote_eph_key.crv = suite_info->dh_curve;
        ctx->remote_eph_key.kty = suite_info->key_type;
    } else if (msg2.g_y_len == 0) {
        EDHOC_FAIL(EDHOC_ERR_INVALID_PARAM);
    } else {
        EDHOC_FAIL(EDHOC_ERR_BUFFER_OVERFLOW);
    }

    // Check the Responder's connection identifier
    if (msg2.cidr_len > 0 && msg2.cidr_len <= EDHOC_CID_MAX_LEN) {
        // do not pass NULL to memcpy -> undefined behavior
        memcpy(ctx->session.cidr, &msg2.cidr, msg2.cidr_len);
    } else {
        EDHOC_FAIL(EDHOC_ERR_BUFFER_OVERFLOW);
    }

    ctx->session.cidr_len = msg2.cidr_len;

    // do some checks on the ciphertext_2
    if (msg2.ciphertext_len > 0 && msg2.ciphertext_len <= EDHOC_PAYLOAD_MAX_SIZE) {
        if (msg2.ciphertext == NULL) {
            EDHOC_FAIL(EDHOC_ERR_INVALID_PARAM);
        }
    } else if (msg2.ciphertext_len == 0) {
        EDHOC_FAIL(EDHOC_ERR_INVALID_PARAM);
    } else {
        EDHOC_FAIL(EDHOC_ERR_BUFFER_OVERFLOW);
    }

    if ((msg1_len = edhoc_msg1_encode(ctx->correlation,
                                      ctx->method,
                                      ctx->session.cipher_suite,
                                      &ctx->local_eph_key,
                                      ctx->session.cidi,
                                      ctx->session.cidi_len,
                                      ctx->conf->ad1,
                                      out,
                                      olen)) <= 0) {
        if (msg1_len < 0) {
            EDHOC_FAIL(msg1_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_SIZE);
        }
    }

    EDHOC_CHECK_SUCCESS(edhoc_compute_th2(out, msg1_len, msg2.data, msg2.data_len, ctx->th_2));
    EDHOC_CHECK_SUCCESS(edhoc_compute_prk2e(&ctx->local_eph_key, &ctx->remote_eph_key, ctx->prk_2e));

    if ((data3_len = edhoc_data3_encode(ctx->correlation, ctx->session.cidr, ctx->session.cidr_len, out, olen)) <= 0) {
        if (data3_len < 0) {
            EDHOC_FAIL(data3_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_SIZE);
        }
    }

    EDHOC_CHECK_SUCCESS(
            crypt_kdf(suite_info->aead_algo, ctx->prk_2e, ctx->th_2, "K_2e", msg2.ciphertext_len, k2e_buf));

    EDHOC_CHECK_SUCCESS(
            edhoc_compute_th3(ctx->th_2, msg2.ciphertext, msg2.ciphertext_len, out, data3_len, ctx->th_3));

    // XOR decryption P_2e XOR K_2e
    for (size_t i = 0; i < msg2.ciphertext_len; i++) {
        msg2.ciphertext[i] = msg2.ciphertext[i] ^ k2e_buf[i];
    }

    EDHOC_CHECK_SUCCESS(edhoc_p2e_decode(&msg2, msg2.ciphertext, msg2.ciphertext_len));

    EDHOC_CHECK_SUCCESS(edhoc_compute_prk3e2m(ctx->method,
                                              ctx->prk_2e,
                                              &ctx->local_eph_key,
                                              &ctx->remote_auth_key,
                                              ctx->prk_3e2m));

    EDHOC_CHECK_SUCCESS(edhoc_compute_prk4x3m(ctx->method,
                                              ctx->prk_3e2m,
                                              &ctx->conf->local_cred.auth_key,
                                              &ctx->remote_eph_key,
                                              ctx->session.prk_4x3m));

    if ((ct3_len = edhoc_create_ciphertext3(suite_info->aead_algo,
                                            ctx->method,
                                            ctx->session.prk_4x3m,
                                            ctx->th_3,
                                            &ctx->conf->local_cred,
                                            ctx->conf->ad3,
                                            ciphertext_3,
                                            EDHOC_PAYLOAD_MAX_SIZE)) <= 0) {
        if (ct3_len < 0) {
            EDHOC_FAIL(ct3_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_SIZE);
        }
    }

    if ((msg3_len = edhoc_msg3_encode(out, data3_len, ciphertext_3, ct3_len, out, olen)) <= 0) {
        if (msg3_len < 0) {
            EDHOC_FAIL(msg3_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_SIZE);
        }
    }

    EDHOC_CHECK_SUCCESS(edhoc_compute_th4(ctx->th_3, ciphertext_3, ct3_len, ctx->session.th_4));

    if (ctx->state == EDHOC_RECEIVED_MESSAGE_2) {
        ctx->state = EDHOC_SENT_MESSAGE_3;
    } else {
        ctx->state = EDHOC_FAILED;
        EDHOC_FAIL(EDHOC_ERR_ILLEGAL_STATE);
    }

    ret = msg3_len;
    exit:
    return ret;
}

ssize_t edhoc_create_msg2(edhoc_ctx_t *ctx, const uint8_t *msg1_buf, size_t msg1_len, uint8_t *out, size_t olen) {
    ssize_t ret;

    edhoc_msg1_t msg1;

    ssize_t data2_len;
    ssize_t msg2_len;

    const cipher_suite_t *suite_info = NULL;

    uint8_t plaintext_2[EDHOC_PAYLOAD_MAX_SIZE];
    ssize_t pt2_len;

    memset(&msg1, 0, sizeof(edhoc_msg1_t));

    if (ctx->state == EDHOC_WAITING) {
        ctx->state = EDHOC_RECEIVED_MESSAGE_1;
    } else {
        ctx->state = EDHOC_FAILED;
        EDHOC_FAIL(EDHOC_ERR_ILLEGAL_STATE);
    }

    // decode message 1
    EDHOC_CHECK_SUCCESS(edhoc_msg1_decode(&msg1, msg1_buf, msg1_len));

    // setup method and correlation values
    ctx->correlation = msg1.method_corr % 4;

    if (ctx->correlation < NO_CORR || ctx->correlation >= CORR_UNSET) {
        EDHOC_FAIL(EDHOC_ERR_INVALID_CORR);
    }

    ctx->method = (msg1.method_corr - ctx->correlation) / 4;

    // load cipher suite
    ctx->session.cipher_suite = msg1.cipher_suite;

    if ((suite_info = edhoc_cipher_suite_from_id(ctx->session.cipher_suite)) == NULL) {
        EDHOC_FAIL(EDHOC_ERR_CIPHERSUITE_UNAVAILABLE);
    }

    // setup Initiator ephemeral public key
    if (msg1.g_x_len <= EDHOC_ECC_KEY_MAX_SIZE && msg1.g_x_len > 0) {
        if (msg1.g_x != NULL) {
            memcpy(ctx->remote_eph_key.x, msg1.g_x, msg1.g_x_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_PARAM);
        }

        // TODO: verify if valid public key
        ctx->remote_eph_key.x_len = msg1.g_x_len;

        ctx->remote_eph_key.crv = suite_info->dh_curve;
        ctx->remote_eph_key.kty = suite_info->key_type;
    } else if (msg1.g_x_len == 0) {
        EDHOC_FAIL(EDHOC_ERR_INVALID_PARAM);
    } else {
        EDHOC_FAIL(EDHOC_ERR_BUFFER_OVERFLOW);
    }

    // setup Initiator connection identifier
    if (msg1.cidi_len <= EDHOC_CID_MAX_LEN && msg1.cidi_len > 0) {
        // fetching security context
    } else if (msg1.cidi_len > EDHOC_CID_MAX_LEN) {
        EDHOC_FAIL(EDHOC_ERR_BUFFER_OVERFLOW);
    }

    ctx->session.cidi_len = msg1.cidi_len;

    if (msg1.ad1_len != 0) {
        // TODO: implement callbacks for ad1 delivery
    }

    // if not already initialized, generate and load ephemeral key
    EDHOC_CHECK_SUCCESS(crypt_gen_keypair(suite_info->dh_curve, &ctx->local_eph_key));

    // generate data_2, must be greater than 0
    if ((data2_len = edhoc_data2_encode(ctx->correlation,
                                        ctx->session.cidi,
                                        ctx->session.cidi_len,
                                        ctx->session.cidr,
                                        ctx->session.cidr_len,
                                        &ctx->local_eph_key,
                                        out,
                                        olen)) <= 0) {

        if (data2_len < 0) {
            EDHOC_FAIL(data2_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_SIZE);
        }
    }

    // compute transcript hash 2: TH_2 = H ( msg1, data_2 )
    EDHOC_CHECK_SUCCESS(edhoc_compute_th2(msg1_buf, msg1_len, out, data2_len, ctx->th_2));

    EDHOC_CHECK_SUCCESS(edhoc_compute_prk2e(&ctx->local_eph_key, &ctx->remote_eph_key, ctx->prk_2e));

    EDHOC_CHECK_SUCCESS(edhoc_compute_prk3e2m(ctx->method,
                                              ctx->prk_2e,
                                              &ctx->conf->local_cred.auth_key,
                                              &ctx->remote_eph_key,
                                              ctx->prk_3e2m));

    if ((pt2_len = edhoc_create_plaintext2(suite_info->aead_algo,
                                                ctx->method,
                                                ctx->prk_3e2m,
                                                ctx->th_2,
                                                &ctx->conf->local_cred,
                                                ctx->conf->ad2,
                                                plaintext_2,
                                                EDHOC_PAYLOAD_MAX_SIZE)) <= 0) {

        if (pt2_len < 0) {
            EDHOC_FAIL(ctx->ct2_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_SIZE);
        }
    }

    ctx->ct2_len = edhoc_create_ciphertext2(suite_info->aead_algo,
                                       ctx->prk_2e,
                                       ctx->th_2,
                                       plaintext_2,
                                       pt2_len,
                                       ctx->ciphertext2);

    if ((msg2_len = edhoc_msg2_encode(out, data2_len, ctx->ciphertext2, ctx->ct2_len, out, olen)) <= 0) {
        if (msg2_len < 0) {
            EDHOC_FAIL(msg2_len);
        } else {
            EDHOC_FAIL(EDHOC_ERR_INVALID_SIZE);
        }
    }

    if (ctx->state == EDHOC_RECEIVED_MESSAGE_1) {
        ctx->state = EDHOC_SENT_MESSAGE_2;
    } else {
        ctx->state = EDHOC_FAILED;
        EDHOC_FAIL(EDHOC_ERR_ILLEGAL_STATE);
    }

    ret = msg2_len;
    exit:
    return ret;
}

int edhoc_init_finalize(edhoc_ctx_t *ctx) {
    int ret;

    if (ctx->state == EDHOC_SENT_MESSAGE_3) {
        ctx->state = EDHOC_FINALIZED;
    } else {
        ctx->state = EDHOC_FAILED;
        EDHOC_FAIL(EDHOC_ERR_ILLEGAL_STATE);
    }

    ret = EDHOC_SUCCESS;
    exit:
    return ret;
}

int edhoc_resp_finalize(edhoc_ctx_t *ctx, const uint8_t *msg3_buf, size_t msg3_len) {
    int ret;
    cose_algo_t aead;
    edhoc_msg3_t msg3;

    const cipher_suite_t *suite_info;
    const aead_info_t *aead_info;

    ssize_t a3ae_len, tag_len;

    uint8_t p3ae_or_ct3_buf[EDHOC_PAYLOAD_MAX_SIZE];
    ssize_t p3ae_or_ct3_len;

    uint8_t k3ae_buf[EDHOC_K23M_MAX_SIZE];
    size_t k3ae_len;

    uint8_t iv3ae_buf[EDHOC_IV23M_MAX_SIZE];
    size_t iv3ae_len;

    uint8_t a_3ae[EDHOC_MAX_A3AE_LEN];

    memset(&msg3, 0, sizeof(edhoc_msg3_t));

    if (ctx->state == EDHOC_SENT_MESSAGE_2) {
        ctx->state = EDHOC_RECEIVED_MESSAGE_3;
    } else {
        ctx->state = EDHOC_FAILED;
        EDHOC_FAIL(EDHOC_ERR_ILLEGAL_STATE);
    }

    if ((suite_info = edhoc_cipher_suite_from_id(ctx->session.cipher_suite)) == NULL)
        return EDHOC_ERR_CIPHERSUITE_UNAVAILABLE;

    aead = suite_info->aead_algo;

    if ((aead_info = cose_aead_info_from_id(aead)) == NULL)
        return EDHOC_ERR_AEAD_CIPHER_UNAVAILABLE;

    k3ae_len = aead_info->key_length;
    iv3ae_len = aead_info->iv_length;

    tag_len = aead_info->tag_length;

    // decode message 1
    EDHOC_CHECK_SUCCESS(edhoc_msg3_decode(&msg3, ctx->correlation, msg3_buf, msg3_len));
    // TODO: copy connection identifier into temporary buffer and verify that it is known.

    if (msg3.ciphertext_len <= sizeof(p3ae_or_ct3_buf)) {
        memcpy(p3ae_or_ct3_buf, msg3.ciphertext, msg3.ciphertext_len);
        p3ae_or_ct3_len = msg3.ciphertext_len;
    } else {
        return EDHOC_ERR_BUFFER_OVERFLOW;
    }

    // compute prk_4x3m
    EDHOC_CHECK_SUCCESS(edhoc_compute_prk4x3m(ctx->method,
                                              ctx->prk_3e2m,
                                              &ctx->local_eph_key,
                                              &ctx->remote_auth_key,
                                              ctx->session.prk_4x3m));


    EDHOC_CHECK_SUCCESS(
            edhoc_compute_th3(ctx->th_2, ctx->ciphertext2, ctx->ct2_len, msg3.data, msg3.data_len, ctx->th_3));

    if ((a3ae_len = edhoc_a3ae_encode(ctx->th_3, a_3ae, EDHOC_MAX_A3AE_LEN)) < EDHOC_SUCCESS) {
        ret = a3ae_len;
        goto exit;
    }

    EDHOC_CHECK_SUCCESS(edhoc_compute_th4(ctx->th_3, p3ae_or_ct3_buf, p3ae_or_ct3_len, ctx->session.th_4));

    EDHOC_CHECK_SUCCESS(crypt_kdf(aead, ctx->session.prk_4x3m, ctx->th_3, "K_3ae", k3ae_len, k3ae_buf));
    EDHOC_CHECK_SUCCESS(
            crypt_kdf(aead, ctx->session.prk_4x3m, ctx->th_3, "IV_3ae", iv3ae_len, iv3ae_buf));

    // decrypt ciphertext_3
    EDHOC_CHECK_SUCCESS(crypt_decrypt(aead,
                                      k3ae_buf,
                                      iv3ae_buf,
                                      a_3ae,
                                      a3ae_len,
                                      p3ae_or_ct3_buf,
                                      p3ae_or_ct3_buf,
                                      p3ae_or_ct3_len - tag_len,
                                      &p3ae_or_ct3_buf[p3ae_or_ct3_len - tag_len]));

    EDHOC_CHECK_SUCCESS(edhoc_p3ae_decode(ctx, p3ae_or_ct3_buf, p3ae_or_ct3_len - tag_len));

    if (ctx->state == EDHOC_RECEIVED_MESSAGE_3) {
        ctx->state = EDHOC_FINALIZED;
    } else {
        ctx->state = EDHOC_FAILED;
        EDHOC_FAIL(EDHOC_ERR_ILLEGAL_STATE);
    }

    exit:
    return ret;
}

int edhoc_compute_th2(const uint8_t *msg1, size_t msg1_len, const uint8_t *data_2, size_t data2_len, uint8_t *th2) {
    int ret;
    hash_ctx_t hash_ctx;

    ret = EDHOC_ERR_CRYPTO;

    EDHOC_CHECK_SUCCESS(crypt_hash_init(&hash_ctx));
    EDHOC_CHECK_SUCCESS(crypt_hash_update(&hash_ctx, msg1, msg1_len));
    EDHOC_CHECK_SUCCESS(crypt_hash_update(&hash_ctx, data_2, data2_len));
    EDHOC_CHECK_SUCCESS(crypt_hash_finish(&hash_ctx, th2));

    exit:
    crypt_hash_free(&hash_ctx);
    return ret;
}

int edhoc_compute_th3(const uint8_t *th2,
                      const uint8_t *ciphertext_2,
                      size_t ct2_len, const uint8_t *data3,
                      size_t data3_len,
                      uint8_t *th3) {
    int ret;

    ssize_t written, size;

    hash_ctx_t hash_ctx;

    uint8_t cbor_enc_buf[EDHOC_PAYLOAD_MAX_SIZE + 2];

    ret = EDHOC_ERR_CRYPTO;

    // Start computation transcript hash 3
    EDHOC_CHECK_SUCCESS(crypt_hash_init(&hash_ctx));

    // update transcript with th_2
    size = 0;
    CBOR_CHECK_RET(cbor_bytes_encode(th2,
                                     EDHOC_HASH_MAX_SIZE,
                                     cbor_enc_buf,
                                     size,
                                     sizeof(cbor_enc_buf)));
    EDHOC_CHECK_SUCCESS(crypt_hash_update(&hash_ctx, cbor_enc_buf, size));

    // update transcript with ciphertext_2
    size = 0;
    CBOR_CHECK_RET(cbor_bytes_encode(ciphertext_2,
                                     ct2_len,
                                     cbor_enc_buf,
                                     size,
                                     sizeof(cbor_enc_buf)));

    EDHOC_CHECK_SUCCESS(crypt_hash_update(&hash_ctx, cbor_enc_buf, size));

    // update transcript with data 3
    EDHOC_CHECK_SUCCESS(crypt_hash_update(&hash_ctx, data3, data3_len));

    // store th_3 in the EDHOC context
    EDHOC_CHECK_SUCCESS(crypt_hash_finish(&hash_ctx, th3));

    exit:
    return ret;
}

int edhoc_compute_th4(const uint8_t *th3, const uint8_t *ciphertext_3, size_t ct3_len, uint8_t *th4) {
    int ret;
    ssize_t written, size;
    hash_ctx_t hash_ctx;

    uint8_t cbor_enc_buf[EDHOC_PAYLOAD_MAX_SIZE + 2];

    ret = EDHOC_ERR_CRYPTO;

    // before decryption of ciphertext_3, start computation TH_4
    EDHOC_CHECK_SUCCESS(crypt_hash_init(&hash_ctx));

    // update transcript with th_3
    size = 0;
    CBOR_CHECK_RET(cbor_bytes_encode(th3,
                                     EDHOC_HASH_MAX_SIZE,
                                     cbor_enc_buf,
                                     size,
                                     sizeof(cbor_enc_buf)));
    EDHOC_CHECK_SUCCESS(crypt_hash_update(&hash_ctx, cbor_enc_buf, size));

    // update transcript with ciphertext_3
    size = 0;
    CBOR_CHECK_RET(cbor_bytes_encode(ciphertext_3,
                                     ct3_len,
                                     cbor_enc_buf,
                                     size,
                                     sizeof(cbor_enc_buf)));

    EDHOC_CHECK_SUCCESS(crypt_hash_update(&hash_ctx, cbor_enc_buf, size));

    // store th_4 in the EDHOC context
    EDHOC_CHECK_SUCCESS(crypt_hash_finish(&hash_ctx, th4));

    exit:
    return ret;
}

int edhoc_compute_prk2e(const cose_key_t *sk, const cose_key_t *pk, uint8_t *prk_2e) {
    return crypt_derive_prk(sk, pk, NULL, 0, prk_2e);
}

int edhoc_compute_prk3e2m(method_t m,
                          const uint8_t *prk_2e,
                          const cose_key_t *sk,
                          const cose_key_t *pk,
                          uint8_t *prk_3e2m) {
    int ret;

    switch (m) {
        case EDHOC_AUTH_SIGN_SIGN:
        case EDHOC_AUTH_STATIC_SIGN:
            memcpy(prk_3e2m, prk_2e, EDHOC_HASH_MAX_SIZE);
            break;
        case EDHOC_AUTH_STATIC_STATIC:
        case EDHOC_AUTH_SIGN_STATIC:
            crypt_derive_prk(sk, pk, prk_2e, EDHOC_HASH_MAX_SIZE, prk_3e2m);
            break;
        default:
            ret = EDHOC_ERR_CRYPTO;
            goto exit;
    }

    ret = EDHOC_SUCCESS;
    exit:
    return ret;
}

int edhoc_compute_prk4x3m(method_t m,
                          const uint8_t *prk_3e2m,
                          const cose_key_t *sk,
                          const cose_key_t *pk,
                          uint8_t *prk_4x3m) {
    int ret;

    switch (m) {
        case EDHOC_AUTH_SIGN_SIGN:
        case EDHOC_AUTH_SIGN_STATIC:
            memcpy(prk_4x3m, prk_3e2m, EDHOC_HASH_MAX_SIZE);
            break;
        case EDHOC_AUTH_STATIC_STATIC:
        case EDHOC_AUTH_STATIC_SIGN:
            crypt_derive_prk(sk, pk, prk_3e2m, EDHOC_HASH_MAX_SIZE, prk_4x3m);
            break;
        default:
            ret = EDHOC_ERR_CRYPTO;
            goto exit;
    }

    ret = EDHOC_SUCCESS;
    exit:
    return ret;
}
