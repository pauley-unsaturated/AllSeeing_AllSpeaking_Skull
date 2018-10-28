/*
 * Random-Access Wave file loader
 */

#include "WavLoader.h"

#include <cstdint>
#include <cstdlib>


namespace {

	struct chunk_tag_t {
		char value[4];
		
		friend bool operator==(const chunk_tag_t& l, const chunk_tag_t& r) {
			return !memcmp(&l.value[0], &r.value[0], sizeof(l.value));
		}
		friend bool operator!=(const chunk_tag_t& l, const chunk_tag_t& r) {
			return !(l == r);
		}
	};
	
	const static chunk_tag_t RIFF_TAG = {'R', 'I', 'F', 'F'};
	const static chunk_tag_t WAVE_TAG = {'W', 'A', 'V', 'E'};
	const static chunk_tag_t FMT_TAG = {'f', 'm', 't', ' '};
	const static chunk_tag_t DATA_TAG {'d', 'a', 't', 'a'};
	
	struct __attribute__((packed)) RIFFChunkHeader
	{
		chunk_tag_t chunk_tag;
		uint32_t chunk_size;
	};
	
}


WavLoader::~WavLoader() {
	
}

bool WavLoader::open(FileWrapper* wrapper) {
	if(!wrapper) {
		return false;
	}

	_file = wrapper;

	/* Open the file and seek to the beginning */
	bool didOpen = _file->open();
	if(!didOpen) {
		_file = NULL;
		return false;
	}
	if (!_file->seek(0)) {
		_file = NULL;
		return false;
	}
	
	/* Read and verify the RIFF / WAV header */
	RIFFChunkHeader header;
	size_t numRead = _file->read(&header, sizeof(header));
	if (numRead != sizeof(header)) {
		_file->close();
		return false;
	}
	if (header.chunk_tag != RIFF_TAG) {
		_file->close();
		return false;
	}
	_file_size = numRead + header.chunk_size;

	/* Verify that this is a WAVE file */
	chunk_tag_t format_tag;
	numRead = _file->read(&format_tag, sizeof(chunk_tag_t));
	if (numRead != sizeof(chunk_tag_t)) {
		_file->close();
		return false;
	}
	if (format_tag != WAVE_TAG) {
		_file->close();
		return false;
	}

	for (;;) {
		/* Read and verify the format chunk */
		numRead = _file->read(&header, sizeof(header));
		if (numRead != sizeof(header)) {
			_file->close();
			return false;
		}
		
		/* Save the location of the next chunk */
		long pos = _file->position();
		long next_chunk_pos = pos + header.chunk_size;
		
		if (header.chunk_tag == FMT_TAG) {
			// Format chunk
			numRead = _file->read(&_format, sizeof(_format));
			if (numRead != sizeof(_format)) {
				_file->close();
				return false;
			}
		}
		else if (header.chunk_tag == DATA_TAG) {
			_length = header.chunk_size / _format.block_align;
			_data_offset = (uint32_t)pos;
		}
		
		/* If we're done, bail */
		if (_file_size <= next_chunk_pos) {
			break;
		}
		/* Otherwise seek to the next chunk */
		if (!_file->seek(next_chunk_pos)) {
			_file->close();
			return false;
		}
	}
	
	_position = 0;
	
	return true;
}

static inline uint32_t MIN(uint32_t a, uint32_t b) {
	return a<b?a:b;
}

static inline uint32_t MAX(uint32_t a, uint32_t b) {
	return a>=b?a:b;
}

uint32_t WavLoader::filePositionForSample(uint32_t sample_pos) {
	uint32_t clippedSamplePos = MIN(sample_pos, numSamples());
	uint32_t result =	_data_offset + clippedSamplePos * frameAlignment();
	return result;
}

uint32_t WavLoader::position() {
	return _position;
}

bool WavLoader::seek(uint32_t position) {
	_position = filePositionForSample(position);
	return _file->seek(_position);
}

uint32_t WavLoader::read(void* buf, uint32_t bufSize) {
	uint32_t num_read = _file->read(buf, bufSize);
	return num_read;
}

void WavLoader::close() {
	if (_file) {
		_file->close();
	}
}
