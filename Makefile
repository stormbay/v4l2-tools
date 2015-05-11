
TEST_VIVI_DUMMY_CAMERA := true

#CROSS_PREFIX := arm-linux-gnueabihf-

CC    := $(CROSS_PREFIX)gcc
AR    := $(CROSS_PREFIX)ar
STRIP := $(CROSS_PREFIX)strip

CFLAGS  := -Wall -O2
LDFLAGS :=

ifneq ($(strip $(CROSS_PREFIX)),)
CFLAGS  += -I$(LINARO_INC_PATH)
LDFLAGS += -L$(LINARO_LIB_PATH)
endif

QUIET := @
FORCE_STATIC := --static

##################################  MicroJSON LIB #####################################

MJSON_VERSION=1.3

MJSON_CFLAGS := $(CFLAGS)
MJSON_CFLAGS += -O2
MJSON_CFLAGS += -fPIC
# Add DEBUG_ENABLE for the tracing code
MJSON_CFLAGS += -DDEBUG_ENABLE -g
# Add TIME_ENABLE to support RFC3339 time literals
MJSON_CFLAGS += -DTIME_ENABLE

MJSON_LDFLAGS := $(LDFLAGS)

MJSON_LIB_OBJS := mjson.o

MJSON_LIB_NAME := libmjson
MJSON_STATIC_LIB := $(MJSON_LIB_NAME)-$(MJSON_VERSION).a
MJSON_SHARED_LIB := $(MJSON_LIB_NAME)-$(MJSON_VERSION).so
MJSON_LIBS := $(MJSON_STATIC_LIB) $(MJSON_SHARED_LIB)

#######################################################################################

CAPTURE_OBJS := capture.o

CAPTURE_MODULE  := v4l2_capture

ifneq ($(strip $(TEST_VIVI_DUMMY_CAMERA)),true)
CFLAGS += -DSKIP_CROP_IOCTL
endif

LDFLAGS += -L. -lmjson


.PHONY: all
all: $(MJSON_LIBS) $(CAPTURE_MODULE)

.PHONY: clean
clean: mjson_clean 
	$(QUIET) rm -f $(CAPTURE_OBJS) $(CAPTURE_MODULE)

$(CAPTURE_OBJS):%o:%c
	$(QUIET) $(CC) -c $(CFLAGS) $< -o $@

$(CAPTURE_MODULE): $(CAPTURE_OBJS)
	@echo "--Compiling '$@' ..."
	$(QUIET) $(CC) $^ -o $@ $(LDFLAGS) $(FORCE_STATIC)
	$(QUIET) $(STRIP) $@

##################################  MicroJSON LIB #####################################

$(MJSON_LIB_OBJS):%o:%c
	$(QUIET) $(CC) -c $(MJSON_CFLAGS) $< $(MJSON_LDFLAGS) $(FORCE_STATIC) -o $@

$(MJSON_STATIC_LIB): $(MJSON_LIB_OBJS)
	@echo "--Compiling '$@' ..."
	$(QUIET) $(AR) -r $@ $^
	$(QUIET) ln -s $@ $(MJSON_LIB_NAME).a

$(MJSON_SHARED_LIB): $(MJSON_LIB_OBJS)
	@echo "--Compiling '$@' ..."
	$(QUIET) $(CC) $^ -shared -o $@
	$(QUIET) ln -s $@ $(MJSON_LIB_NAME).so
	$(QUIET) $(STRIP) $@

mjson_clean:
	$(QUIET) rm -f $(MJSON_LIB_OBJS) $(MJSON_LIBS) $(MJSON_LIB_NAME).a $(MJSON_LIB_NAME).so

#######################################################################################

