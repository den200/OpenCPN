#ifndef OCPN_GLOBJECTS_H
#define OCPN_GLOBJECTS_H

#include <wx/wxprec.h>

#ifdef __OCPN__ANDROID__
#include <GLES2/gl2.h>
#elif defined(__WXQT__) || defined(__WXGTK__)
#include <GL/glew.h>
#else
#include <GL/glew.h>
#endif

#include <string>

/// RAII wrapper for OpenGL buffer objects
class GLBuffer {
public:
    explicit GLBuffer(GLenum target = GL_ARRAY_BUFFER);
    ~GLBuffer();

    GLBuffer(const GLBuffer&) = delete;
    GLBuffer& operator=(const GLBuffer&) = delete;

    GLBuffer(GLBuffer&& other) noexcept;
    GLBuffer& operator=(GLBuffer&& other) noexcept;

    void Bind() const;
    void Unbind() const;
    void Upload(GLsizei size, const void* data, GLenum usage) const;

    GLuint id() const { return m_id; }

private:
    GLuint m_id;
    GLenum m_target;
};

/// RAII wrapper for OpenGL vertex array objects
class GLVertexArray {
public:
    GLVertexArray();
    ~GLVertexArray();

    GLVertexArray(const GLVertexArray&) = delete;
    GLVertexArray& operator=(const GLVertexArray&) = delete;

    GLVertexArray(GLVertexArray&& other) noexcept;
    GLVertexArray& operator=(GLVertexArray&& other) noexcept;

    void Bind() const;
    void Unbind() const;

    GLuint id() const { return m_id; }

private:
    GLuint m_id;
};

/// RAII wrapper for individual OpenGL shader objects
class GLShader {
public:
    GLShader(GLenum type, const std::string& source);
    ~GLShader();

    GLShader(const GLShader&) = delete;
    GLShader& operator=(const GLShader&) = delete;

    GLShader(GLShader&& other) noexcept;
    GLShader& operator=(GLShader&& other) noexcept;

    GLuint id() const { return m_id; }

private:
    GLuint m_id;
};

#endif // OCPN_GLOBJECTS_H
