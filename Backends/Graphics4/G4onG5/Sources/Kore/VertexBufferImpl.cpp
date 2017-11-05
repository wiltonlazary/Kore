#include "pch.h"

#include "VertexBufferImpl.h"

#include <Kore/Graphics4/Graphics.h>

using namespace Kore;

namespace {
	const int multiple = 100;
}

Kore::VertexBufferImpl::VertexBufferImpl(int count, const Graphics4::VertexStructure& structure, int instanceDataStepRate) : _buffer(count * multiple, structure, false, instanceDataStepRate), currentIndex(0), myCount(count) {}

Graphics4::VertexBuffer::VertexBuffer(int count, const VertexStructure& structure, int instanceDataStepRate) : VertexBufferImpl(count, structure, instanceDataStepRate) {}

Graphics4::VertexBuffer::~VertexBuffer() {}

float* Graphics4::VertexBuffer::lock() {
	return _buffer.lock(currentIndex * count(), count());
}

float* Graphics4::VertexBuffer::lock(int start, int count) {
	return _buffer.lock(start + currentIndex * this->count(), count);
}

void Graphics4::VertexBuffer::unlock() {
	_buffer.unlock();

	++currentIndex;
	if (currentIndex >= multiple) {
		currentIndex = 0;
	}
}

int Graphics4::VertexBuffer::count() {
	return myCount;
}

int Graphics4::VertexBuffer::stride() {
	return _buffer.stride();
}
