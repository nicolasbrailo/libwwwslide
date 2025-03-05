#include "wwwslider.h"

#include <curl/curl.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct WwwSlider {
  struct WwwSliderConfig config;
  CURL *curl;
  struct curl_slist *curl_headers;
  pthread_t curl_thread;

  const char *base_url;
  char *client_id;

  char *in_flight_data;
  size_t in_flight_data_sz;
};

static const char *malloc_url(struct WwwSlider *ctx, const char *endpoint) {
  const size_t client_id_sz =
      (ctx->client_id) ? sizeof('/') + strlen(ctx->client_id) : 0;
  const size_t url_sz = strlen(ctx->base_url) + sizeof('/') + strlen(endpoint) +
                        client_id_sz + sizeof('\0');

  char *url = malloc(url_sz);
  if (!url) {
    return NULL;
  }

  if (ctx->client_id) {
    snprintf(url, url_sz, "%s/%s/%s", ctx->base_url, endpoint, ctx->client_id);
  } else {
    snprintf(url, url_sz, "%s/%s", ctx->base_url, endpoint);
  }

  return url;
}

static size_t save_chunk_cb(void *contents, size_t size, size_t nmemb,
                            void *usr) {
  size_t chunk_sz = size * nmemb;
  struct WwwSlider *ctx = (struct WwwSlider *)usr;

  void *reallocd =
      realloc(ctx->in_flight_data, ctx->in_flight_data_sz + chunk_sz + 1);
  if (!reallocd) {
    fprintf(stderr, "Bad alloc: realloc register in_flight_data\n");
    // ctx->in_flight_data will be freed by wwwslider_free
    return 0;
  }

  ctx->in_flight_data = reallocd;
  memcpy(&ctx->in_flight_data[ctx->in_flight_data_sz], contents, chunk_sz);
  ctx->in_flight_data_sz = ctx->in_flight_data_sz + chunk_sz;
  ctx->in_flight_data[ctx->in_flight_data_sz] = '\0';

  return chunk_sz;
}

enum HttpRequest {
  HTTP_RQ_GET,
  HTTP_RQ_POST,
};

static int do_curl_rq(struct WwwSlider *ctx, const char *endpoint,
                      enum HttpRequest rq_type, void *json_payload) {
  if (ctx->in_flight_data || ctx->in_flight_data_sz) {
    fprintf(stderr, "Transfer already in progress\n");
    return -EBUSY;
  }

  const char *url = malloc_url(ctx, endpoint);
  if (!url) {
    fprintf(stderr, "Bad alloc: malloc_url\n");
    return -ENOMEM;
  }

  {
    CURLcode ret = CURLE_OK;
    ret = ret | curl_easy_setopt(ctx->curl, CURLOPT_URL, url);
    switch (rq_type) {
    case HTTP_RQ_GET:
      ret = ret | curl_easy_setopt(ctx->curl, CURLOPT_POST, 0L);
      break;
    case HTTP_RQ_POST:
      ret = ret | curl_easy_setopt(ctx->curl, CURLOPT_POST, 1L);
      if (json_payload) {
        ret =
            ret | curl_easy_setopt(ctx->curl, CURLOPT_POSTFIELDS, json_payload);
      }
      break;
    }

    if (ret != CURLE_OK) {
      fprintf(stderr, "Failed to setup curl for image transfer from %s: %s\n",
              url, curl_easy_strerror(ret));
      goto err;
    }
  }

  {
    const CURLcode ret = curl_easy_perform(ctx->curl);
    if (ret != CURLE_OK) {
      fprintf(stderr, "Failed to transfer image from %s: %s\n", url,
              curl_easy_strerror(ret));
      goto err;
    }
  }

  free((void *)url);
  long http_code = 0;
  curl_easy_getinfo(ctx->curl, CURLINFO_RESPONSE_CODE, &http_code);
  if (http_code == 200) {
    return 0;
  } else {
    return http_code;
  }

  // fallthrough
err:
  free((void *)url);
  free(ctx->in_flight_data);
  ctx->in_flight_data = NULL;
  ctx->in_flight_data_sz = 0;
  return -ECOMM;
}

static void *wwwslider_register(void *usrarg);

struct WwwSlider *wwwslider_init(const char *base_url,
                                 struct WwwSliderConfig config) {
  {
    const int ret = curl_global_init(CURL_GLOBAL_ALL);
    if (ret != 0) {
      fprintf(stderr, "Failed to global init curl: %s\n",
              curl_easy_strerror(ret));
      return NULL;
    }
  }

  struct WwwSlider *ctx = malloc(sizeof(struct WwwSlider));
  if (!ctx) {
    fprintf(stderr, "Bad alloc: ctx\n");
    goto err;
  }

  ctx->curl = NULL;
  ctx->curl_headers = NULL;
  ctx->curl_thread = 0;
  ctx->base_url = NULL;
  ctx->client_id = NULL;
  ctx->in_flight_data = NULL;
  ctx->in_flight_data_sz = 0;
  ctx->config.target_width = config.target_width;
  ctx->config.target_height = config.target_height;
  ctx->config.embed_qr = config.embed_qr;
  ctx->config.request_standalone_qr = config.request_standalone_qr;
  ctx->config.request_metadata = config.request_metadata;
  strncpy(ctx->config.client_id, config.client_id,
          sizeof(ctx->config.client_id));
  ctx->config.on_image_available = config.on_image_available;

  ctx->base_url = strdup(base_url);
  ctx->curl = curl_easy_init();
  if (!ctx->curl || !ctx->base_url) {
    fprintf(stderr, "Bad alloc: ctx handles\n");
    goto err;
  }

  ctx->curl_headers =
      curl_slist_append(ctx->curl_headers, "Content-Type: application/json");
  if (!ctx->curl_headers) {
    fprintf(stderr, "Bad alloc: curl headers\n");
    goto err;
  }

  {
    CURLcode ret = CURLE_OK;
    ret =
        ret | curl_easy_setopt(ctx->curl, CURLOPT_WRITEFUNCTION, save_chunk_cb);
    ret = ret | curl_easy_setopt(ctx->curl, CURLOPT_WRITEDATA, ctx);
    ret = ret | curl_easy_setopt(ctx->curl, CURLOPT_FOLLOWLOCATION, 1L);
    ret = ret |
          curl_easy_setopt(ctx->curl, CURLOPT_HTTPHEADER, ctx->curl_headers);
    if (ret != CURLE_OK) {
      fprintf(stderr, "Failed to setup curl: %s\n", curl_easy_strerror(ret));
      goto err;
    }
  }

  if (pthread_create(&ctx->curl_thread, NULL, wwwslider_register, ctx) != 0) {
    fprintf(stderr, "Error registering: can't launch background thread\n");
    ctx->curl_thread = 0;
    goto err;
  }

  return ctx;

err:
  wwwslider_free(ctx);
  return NULL;
}

void wwwslider_free(struct WwwSlider *ctx) {
  if (!ctx) {
    return;
  }

  if (ctx->curl_thread) {
    pthread_cancel(ctx->curl_thread);
  }

  if (ctx->curl) {
    curl_easy_cleanup(ctx->curl);
    curl_global_cleanup();
    curl_slist_free_all(ctx->curl_headers);
  }

  free((void *)ctx->base_url);
  free(ctx->client_id);
  free(ctx->in_flight_data);
  free(ctx);
}

static void *wwwslider_register(void *usrarg) {
  struct WwwSlider *ctx = usrarg;

  char *client_id_bootstrap = NULL;
  if (ctx->config.client_id[0] != '\0') {
    const size_t sz = strlen(ctx->config.client_id);
    client_id_bootstrap = malloc(sz + 1);
    client_id_bootstrap[sz] = '\0';
    if (!client_id_bootstrap) {
      fprintf(stderr, "Bad alloc: client_id_bootstrap\n");
      goto err;
    }
    strncpy(client_id_bootstrap, ctx->config.client_id,
            strlen(ctx->config.client_id));
    // Force client_id; after the request, this id will be overwritten by
    // whatever the server provided (hopefully the same id we requested)
    ctx->client_id = client_id_bootstrap;
  }

  char json_payload[128];
  {
#define CLIENT_REGISTER_TEMPLATE \
        "{\"target_width\": %zu, \"target_height\": %zu, \"embed_qr\": %s}"
    size_t sz =
        snprintf(json_payload, sizeof(json_payload), CLIENT_REGISTER_TEMPLATE,
                 ctx->config.target_width, ctx->config.target_height,
                 ctx->config.embed_qr ? "true" : "false");
#undef CLIENT_REGISTER_TEMPLATE
    if (sz > sizeof(json_payload)) {
      fprintf(stderr, "Bad request: register request would truncate\n");
      goto err;
    }
  }

  const int rq_ret =
      do_curl_rq(ctx, "client_register", HTTP_RQ_POST, json_payload);
  if (rq_ret < 0) {
    fprintf(stderr, "Failed to setup register request\n");
  } else if (rq_ret > 0) {
    fprintf(stderr, "Failed to register client, HTTP error %d:\n%s\n", rq_ret,
            ctx->in_flight_data);
  } else {
    ctx->client_id = malloc(ctx->in_flight_data_sz + 1);
    strncpy(ctx->client_id, ctx->in_flight_data, ctx->in_flight_data_sz);
    ctx->client_id[ctx->in_flight_data_sz] = '\0';
    free(ctx->in_flight_data);
    ctx->in_flight_data = NULL;
    ctx->in_flight_data_sz = 0;
  }

err:
  free(client_id_bootstrap);
  return NULL;
}

bool wwwslider_wait_registered(struct WwwSlider *ctx) {
  if (!ctx->curl_thread) {
    return ctx->client_id ? true : false;
  }

  if (pthread_join(ctx->curl_thread, NULL) != 0) {
    fprintf(stderr, "Error waiting for client registration thread.\n");
  }

  ctx->curl_thread = 0;
  return ctx->client_id ? true : false;
}

static void wwwslider_request_image(struct WwwSlider *ctx,
                                    const char *endpoint) {
  if (!ctx->client_id) {
    fprintf(stderr, "Requested image but client not registered.\n");
    return;
  }

  void *img_ptr = NULL, *meta_ptr = NULL, *qr_ptr = NULL;
  size_t img_sz = 0, meta_sz = 0, qr_sz = 0;

  {
    const int rq_ret = do_curl_rq(ctx, endpoint, HTTP_RQ_GET, NULL);
    if (rq_ret < 0) {
      fprintf(stderr, "Failed to setup new image request\n");
    } else if (rq_ret > 0) {
      fprintf(stderr, "Failed to register client, HTTP error %d:\n%s\n", rq_ret,
              ctx->in_flight_data);
    } else {
      img_ptr = ctx->in_flight_data;
      img_sz = ctx->in_flight_data_sz;
      ctx->in_flight_data = NULL;
      ctx->in_flight_data_sz = 0;
    }
  }

  if (img_ptr && ctx->config.request_metadata) {
    const int rq_ret =
        do_curl_rq(ctx, "get_current_img_meta", HTTP_RQ_GET, NULL);
    if (rq_ret < 0) {
      fprintf(stderr, "Failed to setup request for image metadata\n");
    } else if (rq_ret > 0) {
      fprintf(stderr, "Failed to fetch image metadata, HTTP error %d:\n%s\n",
              rq_ret, ctx->in_flight_data);
    } else {
      meta_ptr = ctx->in_flight_data;
      meta_sz = ctx->in_flight_data_sz;
      ctx->in_flight_data = NULL;
      ctx->in_flight_data_sz = 0;
    }
  }

  if (img_ptr && ctx->config.request_standalone_qr) {
    const int rq_ret = do_curl_rq(ctx, "get_current_img_qr", HTTP_RQ_GET, NULL);
    if (rq_ret < 0) {
      fprintf(stderr, "Failed to setup request for image QR\n");
    } else if (rq_ret > 0) {
      fprintf(stderr, "Failed to fetch image QR, HTTP error %d:\n%s\n", rq_ret,
              ctx->in_flight_data);
    } else {
      qr_ptr = ctx->in_flight_data;
      qr_sz = ctx->in_flight_data_sz;
      ctx->in_flight_data = NULL;
      ctx->in_flight_data_sz = 0;
    }
  }

  ctx->config.on_image_available(img_ptr, img_sz, meta_ptr, meta_sz, qr_ptr,
                                 qr_sz);

  free(img_ptr);
  free(meta_ptr);
  free(qr_ptr);
}

void wwwslider_get_next_image(struct WwwSlider *ctx) {
  wwwslider_request_image(ctx, "/get_next_img");
}

void wwwslider_get_prev_image(struct WwwSlider *ctx) {
  wwwslider_request_image(ctx, "/get_prev_img");
}
