SOURCE_DIR = ../utils ../bufferinfo ../backend ../compositor ../drm ../hwc2_device ./linux
FILTER = BufferInfoGetter.cpp BufferInfoMapperMetadata.cpp DrmFbImporter.cpp DrmPlane.cpp DrmAtomicStateManager.cpp ResourceManager.cpp HwcLayer.cpp
SOURCE = $(foreach d,$(SOURCE_DIR),$(filter-out $(FILTER), $(filter %.cpp %.c, $(notdir $(wildcard $(d)/*.*)))))
OBJS = $(patsubst %.c, $(OBJ_DIR)/%.o,  $(subst cpp,c,$(SOURCE)))
OBJ_DIR = ./build/objs
BIN_DIR = ./build/bin
BASE_DIR = ../../../../..
BASE_IGT_DIR = $(BASE_DIR)/external/igt-gpu-tools
INC_DIR =   -I/usr/include/drm \
			-I/usr/include/libdrm \
			-I/usr/include/cairo \
			-I$(BASE_IGT_DIR)/lib  \
			-I$(BASE_IGT_DIR)/include/drm-uapi \
			-I$(BASE_DIR)/hardware/libhardware/include \
			-I$(BASE_DIR)/system/core/libsystem/include \
			-I$(BASE_DIR)/system/core/libcutils/include \
			-I$(BASE_DIR)/system/core/libutils/include \
			-I$(BASE_DIR)/frameworks/native/libs/ui/include \
			-I$(BASE_DIR)/system/core/libsync/include \
			-I../hwc2_device \
			-I../bufferinfo \
			-I../drm \
			-I./linux \
			-I../
LINKS_DIR = -L$(IGT_DIR)/build/lib
LINKS_LIB = -ldrm -lpthread -ligt
CPPFLAGS = -std=c++17 -g -DHWC2_USE_CPP11 -DHWC2_INCLUDE_STRINGIFICATION -DPLATFORM_SDK_VERSION=31 -UANDROID
CFLAGS = -g
TARGET = hwc-drm-planes-test

$(TARGET):$(OBJS)
	g++  $(CPPFLAGS) -o $(BIN_DIR)/$@ $^ $(LINKS_DIR)  $(LINKS_LIB)

$(OBJ_DIR)/%.o:../utils/%.cpp
	g++ -c $(CPPFLAGS) $<  $(INC_DIR) -o $@

$(OBJ_DIR)/%.o:../backend/%.cpp
	g++ -c $(CPPFLAGS) $<  $(INC_DIR) -o $@

$(OBJ_DIR)/%.o:../bufferinfo/%.cpp
	g++ -c $(CPPFLAGS) $<  $(INC_DIR) -o $@

$(OBJ_DIR)/%.o:../compositor/%.cpp
	g++ -c $(CPPFLAGS) $<  $(INC_DIR) -o $@

$(OBJ_DIR)/%.o:../drm/%.cpp
	g++ -c $(CPPFLAGS) $<  $(INC_DIR) -o $@

$(OBJ_DIR)/%.o:../hwc2_device/%.cpp
	g++ -c $(CPPFLAGS) $<  $(INC_DIR) -o $@

$(OBJ_DIR)/%.o:./linux/%.c
	gcc -c $(CFLAGS) $<  $(INC_DIR) -o $@

$(OBJ_DIR)/%.o:./linux/%.cpp
	g++ -c $(CPPFLAGS) $<  $(INC_DIR) -o $@

.PHONY:build clean cleanall
build:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)
clean:
	@rm -rf  $(OBJ_DIR)/*.o  $(BIN_DIR)/$(TARGET)
cleanall:
	@rm -rf  ./build
