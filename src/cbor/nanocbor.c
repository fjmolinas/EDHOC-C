#include <edhoc/edhoc.h>

#if defined(NANOCBOR)

#include <nanocbor/nanocbor.h>
#include <memory.h>

#endif

#include "cbor.h"

#if defined(NANOCBOR)

uint8_t cbor_get_type(const uint8_t *buffer, size_t offset, size_t total) {
    nanocbor_value_t decoder;
    uint8_t type;

    memset(&decoder, 0, sizeof decoder);

    nanocbor_decoder_init(&decoder, buffer + offset, total);
    switch (nanocbor_get_type(&decoder)) {
        case NANOCBOR_TYPE_UINT:
            type = CBOR_UINT;
            break;
        case NANOCBOR_TYPE_NINT:
            type = CBOR_NINT;
            break;
        case NANOCBOR_TYPE_BSTR:
            type = CBOR_BSTR;
            break;
        case NANOCBOR_TYPE_TSTR:
            type = CBOR_TSTR;
            break;
        case NANOCBOR_TYPE_ARR:
            type = CBOR_ARRAY;
            break;
        case NANOCBOR_TYPE_MAP:
            type = CBOR_MAP;
            break;
        case NANOCBOR_TYPE_TAG:
            type = CBOR_TAG;
            break;
        case NANOCBOR_TYPE_FLOAT:
            type = CBOR_FLOAT;
            break;
        default:
            type = CBOR_INVALID;
            break;
    }

    return type;
}

/* DECODING ROUTINES */

ssize_t cbor_bstr_id_decode(uint8_t* value, size_t* len, const uint8_t *buffer, size_t offset, size_t total) {
    nanocbor_value_t decoder;
    ssize_t read;
    const uint8_t* tmp;

    memset(&decoder, 0, sizeof decoder);

    nanocbor_decoder_init(&decoder, buffer + offset, total);

    if (nanocbor_get_type(&decoder) == NANOCBOR_TYPE_NINT || nanocbor_get_type(&decoder) == NANOCBOR_TYPE_UINT) {
        if ((read = nanocbor_get_uint8(&decoder, value)) < 0) {
            *len = 0;
            read = EDHOC_ERR_CBOR_DECODING;
        } else {
            value[0] += 24;
            *len = 1;
        }
    } else if (cbor_get_type(buffer, offset, total) == NANOCBOR_TYPE_BSTR) {
        read = nanocbor_get_bstr(&decoder, &tmp, len);
        if (read >= 0) {
            read = (*len + read);
            if (value != NULL && tmp != NULL){
                memcpy(value, tmp, *len);
            } else {
                *len = 0;
                read = EDHOC_ERR_CBOR_DECODING;
            }
        } else {
            *len = 0;
            read = EDHOC_ERR_CBOR_DECODING;
        }
    } else {
        *len = 0;
        read =  EDHOC_ERR_CBOR_DECODING;
    }

    return read;
}

ssize_t cbor_uint_decode(uint8_t *value, const uint8_t *buffer, size_t offset, size_t total) {
    nanocbor_value_t decoder;
    ssize_t read;

    memset(&decoder, 0, sizeof decoder);

    nanocbor_decoder_init(&decoder, buffer + offset, total);
    if ((read = nanocbor_get_uint8(&decoder, (value))) < 0) {
        value = NULL;
        return EDHOC_ERR_CBOR_DECODING;
    } else {
        return read;
    }
}

ssize_t cbor_int_decode(int8_t *value, const uint8_t *buffer, size_t offset, size_t total) {
    nanocbor_value_t decoder;
    ssize_t read;

    memset(&decoder, 0, sizeof decoder);

    nanocbor_decoder_init(&decoder, buffer + offset, total);
    if ((read = nanocbor_get_int8(&decoder, (value))) < 0) {
        value = NULL;
        return EDHOC_ERR_CBOR_DECODING;
    } else {
        return read;
    }
}

ssize_t cbor_bytes_decode(const uint8_t **out, size_t *len, const uint8_t *buffer, size_t offset, size_t total) {
    nanocbor_value_t decoder;
    ssize_t read;

    memset(&decoder, 0, sizeof decoder);

    nanocbor_decoder_init(&decoder, buffer + offset, total);
    if (nanocbor_get_type(&decoder) == NANOCBOR_TYPE_BSTR) {
        read = nanocbor_get_bstr(&decoder, out, len);
        if (read >= 0) {
            return (*len + read);
        } else {
            out = NULL;
            *len = 0;
            return EDHOC_ERR_CBOR_DECODING;
        }
    } else {
        out = NULL;
        *len = 0;
        return EDHOC_ERR_CBOR_DECODING;
    }
}

void cbor_map_get_int_int(int8_t key, int8_t* value, const uint8_t *buffer, size_t offset, size_t total){
    nanocbor_value_t decoder;
    nanocbor_value_t map, _map;
    int _key = 0;

    memset(&decoder, 0, sizeof decoder);

    nanocbor_decoder_init(&decoder, buffer + offset, total);
    nanocbor_enter_map(&decoder, &map);

    while (!nanocbor_at_end(&map)) {
        _map = map;

        if (nanocbor_get_type(&_map) == NANOCBOR_TYPE_UINT || nanocbor_get_type(&_map) == NANOCBOR_TYPE_NINT) {
            if (nanocbor_get_int32(&_map, &_key) < 0) {
                value = NULL;
            } else {
                if (_key == key) {
                    nanocbor_skip(&map);
                    _map = map;
                    if (nanocbor_get_type(&_map) == NANOCBOR_TYPE_UINT ||
                        nanocbor_get_type(&_map) == NANOCBOR_TYPE_NINT) {
                        nanocbor_get_int8(&_map, value);
                    }
                }
            }
        }
        nanocbor_skip(&map);
    }

    value = NULL;
}

void
cbor_map_get_int_bytes(int8_t key, const uint8_t **out, size_t *len, const uint8_t *buffer, size_t offset, size_t total) {
    nanocbor_value_t decoder;
    nanocbor_value_t map, _map;
    int _key = 0;

    memset(&decoder, 0, sizeof decoder);

    nanocbor_decoder_init(&decoder, buffer + offset, total);
    nanocbor_enter_map(&decoder, &map);

    while (!nanocbor_at_end(&map)) {
        _map = map;

        if (nanocbor_get_type(&_map) == NANOCBOR_TYPE_UINT || nanocbor_get_type(&_map) == NANOCBOR_TYPE_NINT) {
            if (nanocbor_get_int32(&_map, &_key) < 0) {
                *len = 0;
                out = NULL;
            } else {
                if (_key == key) {
                    nanocbor_skip(&map);
                    _map = map;
                    if (nanocbor_get_type(&_map) == NANOCBOR_TYPE_BSTR) {
                        if (nanocbor_get_bstr(&_map, out, len) < 0) {
                            *len = 0;
                            out = NULL;
                        } else {
                            return;
                        }
                    }
                }
            }
        }

        nanocbor_skip(&map);
    }

    // reset the length
    *len = 0;
    out = NULL;
}

ssize_t cbor_suites_decode(uint8_t *out, size_t *len, const uint8_t *buffer, size_t offset, size_t total) {
    nanocbor_value_t decoder, map;
    ssize_t read;

    read = 0;
    memset(&decoder, 0, sizeof decoder);

    nanocbor_decoder_init(&decoder, buffer + offset, total);

    if (nanocbor_get_type(&decoder) == NANOCBOR_TYPE_ARR) {
        if (nanocbor_enter_array(&decoder, &map) >= 0) {
            while (!nanocbor_at_end(&map) && (size_t) read < *len) {
                if (nanocbor_get_type(&map) == NANOCBOR_TYPE_UINT) {
                    nanocbor_get_uint8(&map, out + read);
                    read += 1;
                } else {
                    out = NULL;
                    return EDHOC_ERR_CBOR_ENCODING;
                }
            }

            *len = read;
            return read + 1;
        } else {
            out = NULL;
            *len = 0;
            return EDHOC_ERR_CBOR_DECODING;
        }
    } else if (nanocbor_get_type(&decoder) == NANOCBOR_TYPE_UINT) {
        if ((read = nanocbor_get_uint8(&decoder, out)) >= 0) {
            *len = read;
            return read;
        } else {
            out = NULL;
            len = 0;
            return EDHOC_ERR_CBOR_DECODING;
        }
    } else {
        out = NULL;
        len = 0;
        return EDHOC_ERR_CBOR_DECODING;
    }
}

/* ENCODING ROUTINES */

ssize_t cbor_int_encode(int value, uint8_t *buffer, size_t offset, size_t total) {
    nanocbor_encoder_t encoder;

    memset(&encoder, 0, sizeof(nanocbor_encoder_t));

    nanocbor_encoder_init(&encoder, buffer + offset, total - offset);
    if (nanocbor_fmt_int(&encoder, value) < EDHOC_SUCCESS)
        return EDHOC_ERR_CBOR_ENCODING;

    // TODO: check with Koen, why does nanocbor_fmt_int does not return number of written bytes?
    return nanocbor_encoded_len(&encoder);
}

ssize_t cbor_bytes_encode(const uint8_t *bytes, size_t bytes_len, uint8_t *buffer, size_t offset, size_t total) {
    nanocbor_encoder_t encoder;

    memset(&encoder, 0, sizeof(nanocbor_encoder_t));

    nanocbor_encoder_init(&encoder, buffer + offset, total - offset);
    if (nanocbor_put_bstr(&encoder, bytes, bytes_len) < EDHOC_SUCCESS)
        return EDHOC_ERR_CBOR_ENCODING;

    // TODO: check with Koen, why does nanocbor_put_bstr does not return number of written bytes?
    return nanocbor_encoded_len(&encoder);
}

ssize_t cbor_string_encode(const char *string, uint8_t *buffer, size_t offset, size_t total) {
    nanocbor_encoder_t encoder;

    memset(&encoder, 0, sizeof(nanocbor_encoder_t));

    nanocbor_encoder_init(&encoder, buffer + offset, total - offset);
    if (nanocbor_put_tstr(&encoder, string) < EDHOC_SUCCESS)
        return EDHOC_ERR_CBOR_ENCODING;

    // TODO: check with Koen, why does nanocbor_put_bstr does not return number of written bytes?
    return nanocbor_encoded_len(&encoder);
}

ssize_t cbor_create_map(uint8_t *buffer, uint8_t elements, size_t offset, size_t total) {
    nanocbor_encoder_t encoder;

    memset(&encoder, 0, sizeof(nanocbor_encoder_t));

    nanocbor_encoder_init(&encoder, buffer + offset, total - offset);
    if (nanocbor_fmt_map(&encoder, elements) < EDHOC_SUCCESS)
        return EDHOC_ERR_CBOR_ENCODING;

    return nanocbor_encoded_len(&encoder);
}

ssize_t cbor_create_array(uint8_t *buffer, uint8_t elements, size_t offset, size_t total) {
    nanocbor_encoder_t encoder;

    memset(&encoder, 0, sizeof(nanocbor_encoder_t));

    nanocbor_encoder_init(&encoder, buffer + offset, total - offset);
    if (nanocbor_fmt_array(&encoder, elements) < EDHOC_SUCCESS)
        return EDHOC_ERR_CBOR_ENCODING;

    return nanocbor_encoded_len(&encoder);
}

ssize_t cbor_array_append_int(int value, uint8_t *buffer, size_t offset, size_t total) {
    return cbor_int_encode(value, buffer, offset, total);
}

ssize_t cbor_array_append_bytes(const uint8_t *bytes, size_t bytes_len, uint8_t *buffer, size_t offset, size_t total) {
    return cbor_bytes_encode(bytes, bytes_len, buffer, offset, total);
}

ssize_t cbor_array_append_string(const char *string, uint8_t *buffer, size_t offset, size_t total) {
    return cbor_string_encode(string, buffer, offset, total);
}

#endif


