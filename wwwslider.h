#include <stdbool.h>
#include <stddef.h>

typedef void (*on_image_available_cb)(const void *img_ptr, size_t img_sz,
                                      const char *meta_ptr, size_t meta_sz,
                                      const void *qr_ptr, size_t qr_sz);

struct WwwSliderConfig {
  // Ask server to resize images for us
  size_t target_width;
  size_t target_height;
  // Embed a QR into the image with info
  bool embed_qr;
  // Request a separate QR to render locally
  bool request_standalone_qr;
  // Request metadata of current image
  bool request_metadata;
  // Client id; Set to '\0' to request new one, or set to a value to request
  // this id from the server
  char client_id[30];

  on_image_available_cb on_image_available;
};

// Register a new wwwslider client. Will launch a background thread to complete
// the registration.
struct WwwSlider *wwwslider_init(const char *base_url,
                                 struct WwwSliderConfig config);

// Waits until the registration started by wwwslider_init is complete
bool wwwslider_wait_registered(struct WwwSlider *ctx);

// Delete this object; cancels ongoing threads. It's the user's reposnibility to
// wait for ongoing operations.
void wwwslider_free(struct WwwSlider *ctx);

void wwwslider_get_next_image(struct WwwSlider *ctx);
void wwwslider_get_prev_image(struct WwwSlider *ctx);
