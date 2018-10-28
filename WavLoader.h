/*
 * Random-Access Wave file loader
 */

#ifndef WAVLOADER_H
#define WAVLOADER_H

#include <cstdint>
#include <string>

const uint16_t kPCMFormat = 0x01;

struct __attribute__((packed)) WavFormat
{
	uint16_t audio_format;
	uint16_t num_channels;
	uint32_t sample_rate;
	uint32_t data_rate;
	uint16_t block_align;
	uint16_t bits_per_sample;
};

struct __attribute__((packed)) WavFormatExtended
{
	uint16_t valid_bits;
	uint32_t speaker_position_mask;
	char     sub_format[16];
};

struct __attribute__((packed)) SimpleWavHeader
{
	char ChunkID[4];
	uint32_t ChunkSize;
	char Format[4];
	char SubChunk1ID[4];
	uint32_t SubChunk1Size;
	WavFormat format;
	char SubChunk2ID[4];
	uint32_t SubChunk2Size;
};

// Needs to work with
//	open / close AND SdFat.h

class FileWrapper {
 public:

 FileWrapper(std::string fileName)
	 : _fileName(fileName){};

 FileWrapper(const char* fileName)
	 : _fileName(fileName){};
 
	virtual ~FileWrapper(){};
	
	virtual size_t	write(const void* buf, size_t size) = 0;
	virtual size_t	read(void* buf, size_t size) = 0;
	virtual bool		seek(size_t pos) = 0;
	virtual long		position() = 0;
	virtual long		size() = 0;
	
	virtual void		flush() { };
	virtual bool		open() = 0;
	virtual void		close() = 0;
	
	std::string& fileName() { return _fileName; }
	
 private:
	std::string _fileName;
};



#ifdef USE_POSIX
#include <cstdio>

class PosixFileWrapper : public FileWrapper {

 public:
 PosixFileWrapper(std::string fileName)
	 : FileWrapper(fileName), _mode("rw"), _file(NULL) {
		
	}

 PosixFileWrapper(std::string fileName, const char* mode)
	 : FileWrapper(fileName), _mode(mode), _file(NULL) {

	}

	~PosixFileWrapper() {
		if (_file) {
			fclose(_file);
			_file = NULL;
		}
	}

	virtual bool open() {
		if(_file) {
			close();
		}
		_file = fopen(fileName().c_str(), _mode);
		return _file != NULL;
	}
	
	virtual size_t	write(const void* buf, size_t size) {
		if (_file) {
			return fwrite(buf, 1, size, _file);
		}
		return 0;
	}
	
	virtual size_t	read(void* buf, size_t size) {
		if (_file) {
			return fread(buf, 1, size, _file);
		}
		return 0;
	}
	
	virtual bool seek(size_t pos) {
		if (_file) {
			return (-1 != fseek(_file, pos, SEEK_SET));
		}
		return false;
	}

	virtual long position() {
		if(_file) {
			return ftell(_file);
		}
		return -1;
	}
	
	virtual long size() {
		long pos = -1;
		if (_file) {
			long save_pos = position();
			fseek(_file, 0, SEEK_END);
			pos = ftell(_file);
			fseek(_file, save_pos, SEEK_SET);
		}
		return pos;
	}
	
	virtual void flush() {
		if (_file) {
			fflush(_file);
		}
	}

	virtual void close() {
		if (_file) {
			fclose(_file);
			_file = NULL;
		}
	}

 private:
	FILE* _file;
	const char* _mode;
};
#else

//#include <SdFat.h>
#include <Adafruit_SPIFlash.h>
#include <Adafruit_SPIFlash_FatFs.h>

// Arduino / esp8266 file interface wrapper
// TODO: get rid of the virtual functions.
//	We know darn well what kind of object this is.
class SDFileWrapper : public FileWrapper {
 public:
 SDFileWrapper(std::string fileName, Adafruit_W25Q16BV_FatFs& fatfs)
	 : FileWrapper(fileName), _fs(fatfs)
	{
	}

 SDFileWrapper(const char* fileName, Adafruit_W25Q16BV_FatFs& fatfs)
	 : FileWrapper(fileName), _fs(fatfs)
	{
	}
	
	virtual size_t	write(const void* buf, size_t size) {
		if (!_file) {
			return 0;
		}
		size_t result = _file.write((const uint8_t*)buf, size);
		return result;
	}
	
	virtual size_t	read(void* buf, size_t size) {
		if (!_file) {
			return 0;
		}
		// ¯\_(ツ)_/¯ 64k ought to be enough for anybody
		int result = _file.read(buf, (uint16_t)size);
		return (size_t)result;
	}

	virtual bool		seek(size_t pos) {
		if(_file) {
			_file.seek((uint32_t)pos);
		}
	}
	
	virtual long		position() {
		if (_file) {
			return _file.position();
		}
		return -1;
	}
	
	virtual long		size() {
		if (_file) {
			return _file.size();
		}
		return -1;
	}
	
	virtual void		flush() {
		_file.flush();
	}

	virtual bool		open() {
    _file = _fs.open(fileName().c_str(), (uint8_t)FILE_READ);  
    return (bool)(_file);
	}
	
	virtual void		close() {
		if(_file) {
			_file.close();
		}
	}

 private:
	File _file;
  Adafruit_W25Q16BV_FatFs& _fs;
};
#endif

class WavLoader
{
	
 public:

	WavLoader()
		: _format()
		, _file(nullptr)
		, _length(0)
		, _data_offset(0)
		, _file_size(0)
	{};
	
	bool open(FileWrapper* file);
	void close();

	/* Seeks in terms of samples */
	bool seek(uint32_t position);
	uint32_t position();
	
	/* Reads full samples only */
	uint32_t read(void* buf, uint32_t bufSize);
	
	uint32_t sampleRate() { return _format.sample_rate; }
	uint16_t bitsPerSample() { return _format.bits_per_sample; }
	uint16_t numChannels() { return _format.num_channels; }
	uint32_t numSamples() { return _length; }
	uint16_t frameAlignment() { return _format.block_align; }
	
	uint32_t fileSize() { return _file_size; }

	uint32_t filePositionForSample(uint32_t sample_num);
	
	~WavLoader();
	
 private:
	
	WavFormat _format;
	FileWrapper* _file;
	uint32_t _position; // file position in samples
	uint32_t _length;   // file length in samples
	uint32_t _data_offset; // wav data offset in bytes
	uint32_t _file_size; // file size in bytes	
};

#endif // WAVLOADER_H
