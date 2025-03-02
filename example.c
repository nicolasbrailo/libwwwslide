#include "wwwslider.h"

#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

atomic_bool g_user_intr = false;

void handle_user_intr(int sig) { g_user_intr = true; }

void on_image_received(const void *img_ptr, size_t img_sz, const char *meta_ptr,
                       size_t meta_sz, const void *qr_ptr, size_t qr_sz) {
  printf("Received new image%s%s\n", meta_ptr ? ": " : "",
         meta_ptr ? meta_ptr : "");

  FILE *fp = fopen("/home/batman/Downloads/img.jpg", "wb");
  fwrite(img_ptr, 1, img_sz, fp);
  fclose(fp);

  if (qr_ptr) {
    fp = fopen("/home/batman/Downloads/imgqr.jpg", "wb");
    fwrite(qr_ptr, 1, qr_sz, fp);
    fclose(fp);
  }
}

int main() {
  if (signal(SIGINT, handle_user_intr) == SIG_ERR) {
    fprintf(stderr, "Error setting up signal handler\n");
    return 1;
  }

  struct WwwSliderConfig cfg = {
      .target_width = 300,
      .target_height = 200,
      .embed_qr = false,
      .request_standalone_qr = true,
      .request_metadata = true,
      .client_id = "ambience_test",
      .on_image_available = on_image_received,
  };
  struct WwwSlider *slider = wwwslider_init("http://bati.casa:5000", cfg);

  if (!slider || !wwwslider_wait_registered(slider)) {
    fprintf(stderr, "Fail to register\n");
    return 1;
  }

  wwwslider_get_next_image(slider);

  while (!g_user_intr) {
    int ch = getchar();
    if (ch == 'a') {
      printf("Previous image requested.\n");
      wwwslider_get_prev_image(slider);
    } else if (ch == 'd') {
      wwwslider_get_next_image(slider);
      printf("Next image requested.\n");
    }
  }

  wwwslider_free(slider);
  return 0;
}
