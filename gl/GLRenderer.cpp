#define LOG_TAG "hwc-glrenderer"


#include <string.h>
#include <thread>
#include <chrono>
#include "GLRenderer.h"
#include "utils/log.h"
#define DEBUG0(err) ALOGE("failed, glGetError() = %d@%d", err, __LINE__)
#define DEBUG1     { \
    GLenum err = glGetError(); \
    if (err != GL_NO_ERROR) { \
        DEBUG0(err); \
        return false; \
    } \
}

namespace android {
GLRenderer::GLRenderer() {
}

bool GLRenderer::Init(uint32_t w, uint32_t h) {
    if (init_) {
        return true;
    }
    cb_width_ = w;
    cb_height_ = h;
    EGLint num_configs;
    EGLConfig egl_config;
    EGLint const config_attribs[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_NONE};
    EGLint const context_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 0,
        EGL_NONE};
    egl_display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display_ == EGL_NO_DISPLAY) {
        ALOGE("eglGetDisplay failed, glGetError() = %d@%d", glGetError(), __LINE__);
        return false;
    }

    if (!eglInitialize(egl_display_, NULL, NULL)) {
        ALOGE("eglInitialize failed, glGetError() = %d@%d", glGetError(), __LINE__);
        return false;
    }

    if (!eglChooseConfig(egl_display_, config_attribs, &egl_config, 1,
                        &num_configs)) {
        ALOGE("eglChooseConfig failed, glGetError() = %d@%d", glGetError(), __LINE__);
        return false;
    }

    eglContext_ = eglCreateContext(egl_display_, egl_config, EGL_NO_CONTEXT,
                                context_attribs);
    if (eglContext_ == EGL_NO_CONTEXT) {
        ALOGE("eglCreateContext failed, glGetError() = %d@%d", glGetError(), __LINE__);
        return false;
    }
    eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, eglContext_);
    DEBUG1;
    const char* vertexShaderSource =
        "#version 300 es\n"
        "in vec4 position;\n"
        "in vec2 inCoord;\n"
        "out vec2 outCoord;\n"
        "uniform vec2 translation;\n"
        "uniform vec2 scale;\n"
        "uniform vec2 coordTranslation;\n"
        "uniform vec2 coordScale;\n"

        "void main(void) {\n"
        "  gl_Position.xy = position.xy * scale.xy - translation.xy;\n"
        "  gl_Position.zw = position.zw;\n"
        "  outCoord = inCoord * coordScale + coordTranslation;\n"
        "}\n";

    const char *fragmentShaderSource =
        "#version 300 es\n"
        "#define kComposeModeDevice 2\n"
        "precision mediump float;\n"
        "in vec2 outCoord;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D tex;\n"
        "uniform float alpha;\n"
        "uniform int composeMode;\n"
        "uniform vec4 color ;\n"

        "void main(void) {\n"
        "  FragColor = alpha * texture(tex, outCoord);\n"
        "}\n";
    // vertex shader
    int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    DEBUG1;
    GLint textLen = ::strlen(vertexShaderSource);
    glShaderSource(vertexShader, 1, &vertexShaderSource, &textLen);
    DEBUG1;
    glCompileShader(vertexShader);
    DEBUG1;
    GLint success;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        ALOGE("GL_COMPILE_STATUS %d, glGetError() = %d@%d", success, glGetError(), __LINE__);
        return false;
    }
    DEBUG1;
    // fragment shader
    int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    DEBUG1;
    textLen = strlen(fragmentShaderSource);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, &textLen);
    DEBUG1;
    glCompileShader(fragmentShader);
    DEBUG1;
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        ALOGE("GL_COMPILE_STATUS %d, glGetError() = %d@%d", success, glGetError(), __LINE__);
        return false;
    }
    DEBUG1;
    // link shaders
    int shaderProgram = glCreateProgram();
    DEBUG1;
    glAttachShader(shaderProgram, vertexShader);
    DEBUG1;
    glAttachShader(shaderProgram, fragmentShader);
    DEBUG1;
    glLinkProgram(shaderProgram);
    DEBUG1;
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        ALOGE("GL_LINK_STATUS %d, glGetError() = %d@%d", success, glGetError(), __LINE__);
        return false;
    }
    struct Vertex {
        float pos[3];
        float coord[2];
    };

    const Vertex vertices[] = {
        {{ +1, -1, +0 }, { +1, +0 }},
        {{ +1, +1, +0 }, { +1, +1 }},
        {{ -1, +1, +0 }, { +0, +1 }},
        {{ -1, -1, +0 }, { +0, +0 }},

     };

    unsigned int indices[] = {
        0, 1, 2, // first Triangle
        2, 3, 0// second Triangle
    };

    unsigned int VBO, VAO, EBO;
    glGenVertexArraysOES(1, &VAO);
    DEBUG1;
    glGenBuffers(1, &VBO);
    DEBUG1;
    glGenBuffers(1, &EBO);
    DEBUG1;
    glBindVertexArrayOES(VAO);
    DEBUG1;
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    DEBUG1;
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    DEBUG1;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    DEBUG1;
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    DEBUG1;
    glEnableVertexAttribArray(0);
    DEBUG1;
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
    DEBUG1;
    glEnableVertexAttribArray(1);
    DEBUG1;
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(3 * sizeof(float)));
    DEBUG1;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    DEBUG1;

    glBindVertexArrayOES(0);
    DEBUG1;
    glUseProgram(shaderProgram);
    DEBUG1;
    glBindVertexArrayOES(VAO);
    DEBUG1;
    alpha_ = glGetUniformLocation(shaderProgram, "alpha");
    DEBUG1;
    coordtranslation_ = glGetUniformLocation(shaderProgram, "coordTranslation");
    DEBUG1;
    coordscale_ = glGetUniformLocation(shaderProgram, "coordScale");
    DEBUG1;
    scaleslot_ = glGetUniformLocation(shaderProgram, "scale");
    DEBUG1;
    translationslot_ = glGetUniformLocation(shaderProgram, "translation");
    DEBUG1;
    composemode_ = glGetUniformLocation(shaderProgram, "composeMode");
    DEBUG1;
    glUniform1f(alpha_, 1.0);
    DEBUG1;
    glUniform1i(composemode_, 2);
    DEBUG1;
    glUniform2f(translationslot_, 0.0, 0.0);
    DEBUG1;
    glUniform2f(scaleslot_, 1.0, 1.0);
    DEBUG1;
    glUniform2f(coordtranslation_, 0.0, 0.0);
    DEBUG1;
    glUniform2f(coordscale_, 1.0, 1.0);
    DEBUG1;
    glEnable(GL_BLEND);
    DEBUG1;
    glGenTextures(2, textures_);
    DEBUG1;
    glViewport(0, 0, cb_width_, cb_height_);
    DEBUG1;
    return true;
}

bool GLRenderer::Draw(const std::vector<GLLayer> &layers) {
    if (!init_) return false;
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    int index = 0;
    for (auto l : layers) {
        EGLAttrib const attributeList[] = {
            EGL_WIDTH, (GLint)l.cb_width,
            EGL_HEIGHT, (GLint)l.cb_height,
            EGL_LINUX_DRM_FOURCC_EXT, (GLint)l.format,
            EGL_DMA_BUF_PLANE0_FD_EXT, (GLint)l.fd,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
            EGL_DMA_BUF_PLANE0_PITCH_EXT, (GLint)l.pitch,
            EGL_NONE};
        EGLImage image = eglCreateImage(egl_display_,
                                        NULL,
                                        EGL_LINUX_DMA_BUF_EXT,
                                        (EGLClientBuffer)NULL,
                                        attributeList);
        if(image == EGL_NO_IMAGE) {
            ALOGE("eglCreateImage failed, glGetError() = %d@%d", glGetError(), __LINE__);
            return false;
        }
        DEBUG1;
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
        DEBUG1;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        DEBUG1;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        DEBUG1;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        DEBUG1;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        DEBUG1;
        float edges[4];
        edges[0] = 1 - 2.0 * (cb_width_ - l.display_frame.left)/cb_width_;
        edges[1] = 1 - 2.0 * (cb_height_ -  l.display_frame.top)/cb_height_;
        edges[2] = 1 - 2.0 * (cb_width_ -  l.display_frame.right)/cb_width_;
        edges[3] = 1 - 2.0 * (cb_height_ -  l.display_frame.bottom)/cb_height_;

        float crop[4];
        crop[0] = (float)l.source_crop.left / l.cb_width;
        crop[1] = (float)l.source_crop.top / l.cb_height;
        crop[2] = (float)l.source_crop.right / l.cb_width;
        crop[3] = (float)l.source_crop.bottom / l.cb_height;

        // setup the |translation| uniform value.
        glUniform2f(translationslot_, (-edges[2] - edges[0])/2,
                            (-edges[3] - edges[1])/2);
        DEBUG1;
        glUniform2f(scaleslot_, (edges[2] - edges[0])/2,
                            (edges[1] - edges[3])/2);
        DEBUG1;
        glUniform2f(coordtranslation_, crop[0], crop[3]);
        DEBUG1;
        glUniform2f(coordscale_, crop[2] - crop[0], crop[1] - crop[3]);
        DEBUG1;
        glEnable(GL_BLEND);
        DEBUG1;
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        DEBUG1;
        glBindTexture(GL_TEXTURE_2D, textures_[index]);
        DEBUG1;
        glActiveTexture(GL_TEXTURE0 + index);
        DEBUG1;
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        DEBUG1;
        eglDestroyImage(egl_display_,image);
        DEBUG1;
    }
    glFlush();
    DEBUG1;
    glFinish();
    DEBUG1;
    return true;
}

bool GLRenderer::InitSuperFrameEnv(std::optional<BufferInfo> bi) {
  if (init_) {
    return true;
  }
  const EGLAttrib attr_list[] = {EGL_WIDTH, (GLint)bi->width,
                              EGL_HEIGHT,(GLint) bi->height,
                              EGL_LINUX_DRM_FOURCC_EXT, (GLint)bi->format,
                              EGL_DMA_BUF_PLANE0_FD_EXT, (GLint)bi->prime_fds[0],
                              EGL_DMA_BUF_PLANE0_PITCH_EXT, (GLint)bi->pitches[0],
                              EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
                              EGL_NONE,  0};
  if (image_ == EGL_NO_IMAGE) 
    image_ = eglCreateImage(egl_display_, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
                            static_cast<EGLClientBuffer>(nullptr), attr_list);
  if (image_ == EGL_NO_IMAGE) {
    ALOGE("eglCreateImage failed, glGetError() = %d@%d", glGetError(), __LINE__);
    return false;
  }
  if (!texture_)
    glGenTextures(1, &texture_);
  DEBUG1;
  glBindTexture(GL_TEXTURE_2D, texture_);
  DEBUG1;

  glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image_);
  DEBUG1;
  glBindTexture(GL_TEXTURE_2D, 0);
  DEBUG1;

  if (!fb_)
    glGenFramebuffers(1, &fb_);
  DEBUG1;
  glBindFramebuffer(GL_FRAMEBUFFER, fb_);
  DEBUG1;
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture_, 0);
  DEBUG1;
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    switch (status) {
      case (GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT):
        ALOGE("GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT.");
        break;
      case (GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT):
        ALOGE("GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT.");
        break;
      case (GL_FRAMEBUFFER_UNSUPPORTED):
        ALOGE("GL_FRAMEBUFFER_UNSUPPORTED.");
        break;
      default:
        break;
    }
    return false;
  }
  init_ = true;
  return true;
}

bool GLRenderer::ReInitSuperFrameEnv(std::optional<BufferInfo> bi) {
  eglDestroyImage(egl_display_,image_);
  glDeleteTextures(1, &texture_);
  glDeleteFramebuffers(1, &fb_);
  image_ = EGL_NO_IMAGE;
  texture_ = 0;
  fb_ = 0;
  init_ = false;
  return InitSuperFrameEnv(bi);
}

bool GLRenderer::CheckFrameBufferStatus() {
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  return status == GL_FRAMEBUFFER_COMPLETE;
}
}