#ifndef ANDROID_GLRENDERER_H_
#define ANDROID_GLRENDERER_H_

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <vector>
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <optional>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "hwcomposer_defs.h"
#include "bufferinfo/BufferInfo.h"
#define SUPER_FRAME_LAYER_COUNT 3
namespace android {

class GLRenderer {
public:
struct GLLayer {
  uint32_t cb_width;
  uint32_t cb_height;
  uint32_t format;
  uint32_t pitch;
  uint32_t fd;
  hwc_rect_t display_frame;
  hwc_frect_t source_crop;
};
public:
  GLRenderer();
  ~GLRenderer() = default;
  bool Init(uint32_t w, uint32_t height);
  bool Draw(const std::vector<GLLayer> &layers);
  bool InitSuperFrameEnv(const std::optional<BufferInfo> bi, uint16_t id);
  bool ReInitSuperFrameEnv(const std::optional<BufferInfo> bi, uint16_t id);
  bool CheckFrameBufferStatus();
private:
  EGLDisplay egl_display_;
  EGLContext eglContext_;
  GLint alpha_;
  GLint coordtranslation_;
  GLint coordscale_;
  GLint scaleslot_;
  GLint translationslot_;
  GLint composemode_;
  GLuint textures_[2];
  uint32_t cb_width_;
  uint32_t cb_height_;
  bool init_[SUPER_FRAME_LAYER_COUNT] = {false};
  EGLImage image_[SUPER_FRAME_LAYER_COUNT] = {EGL_NO_IMAGE};
  GLuint texture_[SUPER_FRAME_LAYER_COUNT] = {0};
  GLuint fb_[SUPER_FRAME_LAYER_COUNT] = {0};
  uint16_t  superframe_layer_id_ = 0;
};
}
#endif