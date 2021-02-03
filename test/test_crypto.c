#include <stdint.h>
#include <assert.h>
#include <string.h>

#include "cipher_suites.h"

#include "util.h"
#include "json.h"
#include "crypto.h"

#if defined(WOLFSSL)

#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/random.h>

#endif /* WOLFSSL */

int test_hashing(const uint8_t *msg1, size_t msg1_len, const uint8_t *data2, size_t data2_len, const uint8_t *th2) {
    ssize_t ret;
    uint8_t buf[32];

    hash_ctx_t hash_ctx;

    crypt_hash_init(&hash_ctx);

    crypt_hash_update(&hash_ctx, msg1, msg1_len);
    crypt_hash_update(&hash_ctx, data2, data2_len);

    crypt_hash_finish(&hash_ctx, buf);

    CHECK_TEST_RET_EQ(compare_arrays(buf, th2, EDHOC_HASH_MAX_SIZE), (long) 0);

    exit:
    return ret;
}

int test_ecdh_computation(cose_curve_t crv, const uint8_t *priv_key, size_t priv_key_len, const uint8_t *pub_key,
                          size_t pub_key_len, const uint8_t *secret, size_t secret_len) {
    ssize_t ret;
    uint8_t outbuf[EDHOC_ECC_KEY_MAX_SIZE];

    edhoc_ctx_t ctx;
    edhoc_conf_t conf;

    cose_key_t private_key;
    cose_key_t public_key;

    edhoc_ctx_init(&ctx);
    edhoc_conf_init(&conf);

    cose_key_init(&public_key);
    cose_key_init(&private_key);

#if defined(MBEDTLS)
    char *pers = "edhoc_responder";
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    EDHOC_CHECK_SUCCESS(mbedtls_ctr_drbg_seed(&ctr_drbg,
                                          mbedtls_entropy_func,
                                          &entropy,
                                          (const unsigned char *) pers,
                                          strlen(pers)));


    EDHOC_CHECK_SUCCESS(edhoc_conf_setup(&conf, EDHOC_IS_RESPONDER, mbedtls_entropy_func, &entropy, NULL));
#elif defined(WOLFSSL)
    RNG rng;

    if (wc_InitRng(&rng) != EDHOC_SUCCESS)
        exit(-1);

    EDHOC_CHECK_SUCCESS(edhoc_conf_setup(&conf, EDHOC_IS_RESPONDER, NULL, &rng, NULL, NULL, NULL, NULL));
#endif

    // loading the configuration
    edhoc_ctx_setup(&ctx, &conf);

    // manually load remote ephemeral public key
    cose_key_from_cbor(&private_key, priv_key, priv_key_len);

    // manually load remote ephemeral public key
    cose_key_from_cbor(&public_key, pub_key, pub_key_len);

    CHECK_TEST_RET_EQ(crypt_compute_ecdh(
            COSE_EC_CURVE_X25519,
            &private_key,
            &public_key,
            ctx.conf->f_rng,
            ctx.conf->p_rng,
            outbuf), (long) 0);

    CHECK_TEST_RET_EQ(compare_arrays(outbuf, secret, secret_len), (long) 0);

    exit:
#if defined(MBEDTLS)
    // TODO: free mbedtls structures
#elif defined(WOLFSSL)
    wc_FreeRng(&rng);
#elif defined(HACL)
#endif
    return ret;
}

int test_key_generation(void) {
    int ret;
    cose_key_t key;

    edhoc_ctx_t ctx;
    edhoc_conf_t conf;

    edhoc_ctx_init(&ctx);
    edhoc_conf_init(&conf);

#if defined(WOLFSSL)

#elif defined(MBEDTLS)
    char *pers = "edhoc_responder";
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    EDHOC_CHECK_SUCCESS(mbedtls_ctr_drbg_seed(&ctr_drbg,
                                          mbedtls_entropy_func,
                                          &entropy,
                                          (const unsigned char *) pers,
                                          strlen(pers)));

    EDHOC_CHECK_SUCCESS(edhoc_conf_setup(&conf, EDHOC_IS_INITIATOR, mbedtls_entropy_func, &entropy, NULL));
#endif

    // loading the configuration
    edhoc_ctx_setup(&ctx, &conf);

    if ((ret = crypt_gen_keypair(COSE_EC_CURVE_X25519, ctx.conf->f_rng, ctx.conf->p_rng, &key)) != EDHOC_SUCCESS) {
        goto exit;
    }

    ret = 0;
    exit:
    return ret;
}

int test_compute_prk2e(uint8_t secret[32], const uint8_t *salt, size_t salt_size, const uint8_t *result) {
    (void) salt;
    ssize_t ret;
    uint8_t out[32];

    CHECK_TEST_RET_EQ(crypt_compute_prk2e(secret, salt, salt_size, out), (long) 0);
    CHECK_TEST_RET_EQ(compare_arrays(out, result, 32), (long) 0);

    exit:
    return ret;
}

int test_compute_ed25519_signature(uint8_t *sk, size_t sk_len, uint8_t *m_2, size_t m_2_len, uint8_t *expected) {
    ssize_t ret;
    cose_key_t authkey;
    cose_key_init(&authkey);

    uint8_t signature[64];

    cose_key_from_cbor(&authkey, sk, sk_len);

    CHECK_TEST_RET_EQ(crypt_compute_signature(COSE_EC_CURVE_ED25519, &authkey, m_2, m_2_len, NULL, NULL, signature),
                      (long) 0);
    CHECK_TEST_RET_EQ(compare_arrays(signature, expected, EDHOC_SIG23_MAX_SIZE), (long) 0);

    exit:
    return ret;
}

int test_edhoc_kdf(
        cose_algo_t id,
        uint8_t *prk,
        uint8_t *transcript,
        const char *label,
        uint8_t *expected,
        size_t expected_len) {

    ssize_t ret;
    uint8_t out[expected_len];

    CHECK_TEST_RET_EQ(crypt_edhoc_kdf(id, prk, transcript, label, expected_len, out), (long) 0);
    CHECK_TEST_RET_EQ(compare_arrays(expected, out, expected_len), (long) 0);

    exit:
    return ret;
}

int main(int argc, char **argv) {

    /* buffers */
    int ret;
    test_context_ptr ctx;

    cose_algo_t id;

    uint8_t m1[MESSAGE_1_SIZE]; size_t msg1_len;
    uint8_t data_2[DATA_2_SIZE]; size_t data2_len;
    uint8_t th_2[TH_SIZE]; size_t th2_len;
    uint8_t salt[TH_SIZE]; size_t salt_len;
    uint8_t prk2e[TH_SIZE]; size_t prk2e_len;
    uint8_t prk3e2m[TH_SIZE]; size_t prk3e2m_len;
    uint8_t k2m[SYMMETRIC_KEY_SIZE]; size_t k2m_len;
    uint8_t iv2m[IV_SIZE]; size_t iv2m_len;
    uint8_t resp_authkey[AUTHKEY_SIZE]; size_t resp_authkey_len;
    uint8_t m2[M2_SIZE]; size_t m2_len;
    uint8_t sig[SIGNATURE_SIZE]; size_t sig_len;
    uint8_t k2e[PAYLOAD_SIZE]; size_t k2e_len;
    uint8_t init_ephkey[EPHKEY_SIZE]; size_t init_ephkey_len;
    uint8_t resp_ephkey[EPHKEY_SIZE]; size_t resp_ephkey_len;
    uint8_t secret[SECRET_SIZE]; size_t secret_len;

    int selected;

    /* test selection */

    ret = 0;

    if (argc == 3) {
        if (strcmp(argv[1], "--hashing") == 0) {
            ctx = load_json_test_file(argv[2]);
            assert(ctx != NULL);

            msg1_len = load_from_json_MESSAGE1(ctx, m1, sizeof(m1));
            data2_len = load_from_json_DATA2(ctx, data_2, sizeof(data_2));
            th2_len = load_from_json_TH2(ctx, th_2, sizeof(th_2));

            assert(msg1_len >= 0);
            assert(data2_len >= 0);
            assert(th2_len == EDHOC_HASH_MAX_SIZE);

            ret = test_hashing(m1, msg1_len, data_2, data2_len, th_2);

            close_test(ctx);

        } else if (strcmp(argv[1], "--ecdh") == 0) {
            ctx = load_json_test_file(argv[2]);
            assert(ctx != NULL);

            assert(load_from_json_CIPHERSUITE(ctx, &selected) == 0);

            init_ephkey_len = load_from_json_INIT_EPHKEY(ctx, init_ephkey, sizeof(init_ephkey));
            resp_ephkey_len = load_from_json_RESP_EPHKEY(ctx, resp_ephkey, sizeof(resp_ephkey));
            secret_len = load_from_json_DH_SECRET(ctx, secret, sizeof(secret));

            assert(init_ephkey_len >= 0);
            assert(resp_ephkey_len >= 0);
            assert(secret_len > 0);

            ret = test_ecdh_computation(edhoc_cipher_suite_from_id(selected)->dh_curve,
                                        init_ephkey,
                                        init_ephkey_len,
                                        resp_ephkey,
                                        resp_ephkey_len,
                                        secret,
                                        secret_len);
            close_test(ctx);

        } else if (strcmp(argv[1], "--hmac") == 0) {
            ctx = load_json_test_file(argv[2]);
            assert(ctx != NULL);

            secret_len = load_from_json_DH_SECRET(ctx, secret, sizeof(secret));
            prk2e_len = load_from_json_PRK2E(ctx, prk2e, sizeof(prk2e));
            salt_len = load_from_json_RESP_SALT(ctx, salt, sizeof(salt));

            assert(secret_len >= 0);
            assert(prk2e_len == EDHOC_HASH_MAX_SIZE);
            assert(salt_len == 0);

            ret = test_compute_prk2e(secret, salt, salt_len, prk2e);

            close_test(ctx);
        } else if (strcmp(argv[1], "--edhoc-kdf-k2m") == 0) {
            const char *label = "K_2m";
            ctx = load_json_test_file(argv[2]);
            assert(ctx != NULL);

            assert(load_from_json_CIPHERSUITE(ctx, (int *) &selected) == 0);

            prk3e2m_len = load_from_json_PRK3E2M(ctx, prk3e2m, sizeof(prk3e2m));
            th2_len = load_from_json_TH2(ctx, th_2, sizeof(th_2));
            k2m_len = load_from_json_K2M(ctx, k2m, sizeof(k2m));

            assert(prk3e2m_len == EDHOC_HASH_MAX_SIZE);
            assert(th2_len >= EDHOC_HASH_MAX_SIZE);
            assert(k2m_len >= 0);

            id = edhoc_cipher_suite_from_id(selected)->aead_algo;

            ret = test_edhoc_kdf(id, prk3e2m, th_2, label, k2m, k2m_len);

            close_test(ctx);
        } else if (strcmp(argv[1], "--edhoc-kdf-iv2m") == 0) {
            const char *label = "IV_2m";
            ctx = load_json_test_file(argv[2]);
            assert(ctx != NULL);

            load_from_json_CIPHERSUITE(ctx, (int *) &selected);
            prk3e2m_len = load_from_json_PRK3E2M(ctx, prk3e2m, sizeof(prk3e2m));
            th2_len = load_from_json_TH2(ctx, th_2, sizeof(th_2));
            iv2m_len = load_from_json_IV2M(ctx, iv2m, sizeof(iv2m));

            assert(prk3e2m_len == EDHOC_HASH_MAX_SIZE);
            assert(th2_len >= EDHOC_HASH_MAX_SIZE);
            assert(iv2m_len >= 0);

            id = edhoc_cipher_suite_from_id(selected)->aead_algo;

            ret = test_edhoc_kdf(id, prk3e2m, th_2, label, iv2m, iv2m_len);

            close_test(ctx);
        } else if (strcmp(argv[1], "--edhoc-kdf-k2e") == 0) {
            const char *label = "K_2e";
            ctx = load_json_test_file(argv[2]);
            assert(ctx != NULL);

            assert(load_from_json_CIPHERSUITE(ctx, (int *) &selected) == 0);

            prk2e_len = load_from_json_PRK2E(ctx, prk2e, sizeof(prk2e));
            th2_len = load_from_json_TH2(ctx, th_2, sizeof(th_2));
            k2e_len = load_from_json_K2E(ctx, k2e, sizeof(k2e));

            assert(prk2e_len == EDHOC_HASH_MAX_SIZE);
            assert(th2_len >= EDHOC_HASH_MAX_SIZE);
            assert(k2e_len >= 0);

            id = edhoc_cipher_suite_from_id(selected)->aead_algo;

            ret = test_edhoc_kdf(id, prk2e, th_2, label, k2e, k2e_len);

            close_test(ctx);
        } else if (strcmp(argv[1], "--ed25519") == 0) {
            ctx = load_json_test_file(argv[2]);
            assert(ctx != NULL);

            resp_authkey_len = load_from_json_RESP_AUTHKEY(ctx, resp_authkey, sizeof(resp_authkey));
            m2_len = load_from_json_M2(ctx, m2, sizeof(m2));
            sig_len = load_from_json_SIGNATURE(ctx, sig, sizeof(sig));

            assert(resp_authkey_len >= 0);
            assert(m2_len >= 0);
            assert(sig_len >= 0);

            ret = test_compute_ed25519_signature(resp_authkey, resp_authkey_len, m2, m2_len, sig);

            close_test(ctx);
        }
    }

    return ret;
}