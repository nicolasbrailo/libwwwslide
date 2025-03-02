SYSROOT=/home/batman/src/xcomp-rpiz-env/mnt/
XCOMPILE=\
	 -target arm-linux-gnueabihf \
	 -mcpu=arm1176jzf-s \
	 --sysroot $(SYSROOT)

# Uncomment to build to local target:
XCOMPILE=


CFLAGS= \
	$(XCOMPILE) \
	-fdiagnostics-color=always \
	-ffunction-sections -fdata-sections \
	-ggdb -O0 \
	-I.
	-std=gnu99 \
	-Wall -Werror \
	-Wendif-labels \
	-Wextra \
	-Wfloat-equal \
	-Wformat=2 \
	-Wimplicit-fallthrough \
	-Winit-self \
	-Winvalid-pch \
	-Wmissing-field-initializers \
	-Wmissing-include-dirs \
	-Wno-strict-prototypes \
	-Wno-unused-function \
	-Woverflow \
	-Wpedantic  \
	-Wpointer-arith \
	-Wredundant-decls \
	-Wstrict-aliasing=2 \
	-Wundef \
	-Wuninitialized \


LDFLAGS=-Wl,--gc-sections -lcurl

example: \
		build/example.o \
		build/wwwslider.o
	clang $(CFLAGS) $(LDFLAGS) $^ -o $@

clean:
	rm -rf build

build/%.o: %.c
	mkdir -p $(shell dirname $@)
	clang $(CFLAGS) -c $^ -o $@

.PHONY: xcompile-start xcompile-end xcompile-rebuild-sysrootdeps

xcompile-start:
	./rpiz-xcompile/mount_rpy_root.sh ~/src/xcomp-rpiz-env

xcompile-end:
	./rpiz-xcompile/umount_rpy_root.sh ~/src/xcomp-rpiz-env

install_sysroot_deps:
	./rpiz-xcompile/add_sysroot_pkg.sh ~/src/xcomp-rpiz-env http://raspbian.raspberrypi.com/raspbian/pool/main/c/curl/libcurl4-openssl-dev_7.88.1-10+rpi1+deb12u8_armhf.deb

.PHONY: deploy run
deploy: example
	scp eink StoneBakedMargheritaHomeboard:/home/batman/example
run: deploy
	ssh StoneBakedMargheritaHomeboard /home/batman/example


