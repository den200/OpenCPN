#include "glObjects.h"
#include <utility>

GLBuffer::GLBuffer(GLenum target) : m_id(0), m_target(target) {
    glGenBuffers(1, &m_id);
}

GLBuffer::~GLBuffer() {
    if (m_id) glDeleteBuffers(1, &m_id);
}

GLBuffer::GLBuffer(GLBuffer&& other) noexcept : m_id(other.m_id), m_target(other.m_target) {
    other.m_id = 0;
}

GLBuffer& GLBuffer::operator=(GLBuffer&& other) noexcept {
    if (this != &other) {
        if (m_id) glDeleteBuffers(1, &m_id);
        m_id = other.m_id;
        m_target = other.m_target;
        other.m_id = 0;
    }
    return *this;
}

void GLBuffer::Bind() const { glBindBuffer(m_target, m_id); }

void GLBuffer::Unbind() const { glBindBuffer(m_target, 0); }

void GLBuffer::Upload(GLsizei size, const void* data, GLenum usage) const {
    glBufferData(m_target, size, data, usage);
}

GLVertexArray::GLVertexArray() : m_id(0) {
    glGenVertexArrays(1, &m_id);
}

GLVertexArray::~GLVertexArray() {
    if (m_id) glDeleteVertexArrays(1, &m_id);
}

GLVertexArray::GLVertexArray(GLVertexArray&& other) noexcept : m_id(other.m_id) {
    other.m_id = 0;
}

GLVertexArray& GLVertexArray::operator=(GLVertexArray&& other) noexcept {
    if (this != &other) {
        if (m_id) glDeleteVertexArrays(1, &m_id);
        m_id = other.m_id;
        other.m_id = 0;
    }
    return *this;
}

void GLVertexArray::Bind() const { glBindVertexArray(m_id); }

void GLVertexArray::Unbind() const { glBindVertexArray(0); }

GLShader::GLShader(GLenum type, const std::string& source) : m_id(0) {
    m_id = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(m_id, 1, &src, nullptr);
    glCompileShader(m_id);
}

GLShader::~GLShader() {
    if (m_id) glDeleteShader(m_id);
}

GLShader::GLShader(GLShader&& other) noexcept : m_id(other.m_id) {
    other.m_id = 0;
}

GLShader& GLShader::operator=(GLShader&& other) noexcept {
    if (this != &other) {
        if (m_id) glDeleteShader(m_id);
        m_id = other.m_id;
        other.m_id = 0;
    }
    return *this;
}

