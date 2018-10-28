/*
 * AudioStream.h
 *
 * An Unsaturated audio library
 * optimized for embedded use.
 * 
 * © 2017 - Mark Pauley
 * 
 */

#ifndef UNS_AUDIOSTREAM_H
#define UNS_AUDIOSTREAM_H

#include "WavLoader.h"
#include <cstdint>
#include <vector>
#include <limits>

namespace Unsaturated {

	/*
	 * Your typical "Pull" stream
	 */
	template <typename SubType,
						typename SampleType>
		class AudioInputStream {
	public:
  
		int read(SampleType* buf, unsigned int num_samples) {
			return static_cast<SubType>(this)->read(buf, num_samples);
		}

		operator bool() const {
			return (bool)(static_cast<SubType>(this));
		}
	};

	enum AudioSamplerError : uint8_t
	{
		 NoErr = 0,
		 BadFile = 1,
		 BadSampleSize = 2,
	};
	/*
	 * Simple version 1 non-pitch stretching sampler.
	 * optimized to play the beginning of the file quickly.
	 */
	template <typename SampleType>
		class AudioSamplerStream
		: public AudioInputStream<AudioSamplerStream<SampleType>, SampleType> {
		using ThisClass = AudioSamplerStream<SampleType>;
	
	public:
		static constexpr int BlockSize = 4096;
		static constexpr int NumBlocks = 3;
		static constexpr int SamplesPerBlock = BlockSize/sizeof(SampleType);

		AudioSamplerStream() : _file(), _introBufSize(0) { }

	public:

		unsigned int sample_rate() { return _file.sampleRate(); };
		unsigned int num_channels() { return _file.numChannels(); };
    unsigned int sample_index() { return _sampleIdx; }
    bool set_sample_index(uint32_t sampleIndex) { 
      if (sampleIndex < _file.numSamples()) {
        _sampleIdx = sampleIndex;
      }
    }
		
		AudioSamplerError load(FileWrapper* file) {
			/* 
			 * Load the file and fail if the sample format is incompatible
			 */
			if(_file.open(file)) {
				if (_file.bitsPerSample() != sizeof(SampleType) * 8) {
					return AudioSamplerError::BadSampleSize;
				}

				loadIntroBuffer();
				_readHead = _introBuf;
				_sampleIdx = 0;
				for (int i = 0; i < NumBlocks; i++) {
					_bufBlockMap[i] = UnmappedBlock;
				}
		
				return AudioSamplerError::NoErr;
			}
			return AudioSamplerError::BadFile;
		}		
		
		int read(SampleType* buf, unsigned int numSamples) {	  
			unsigned int numSamplesLeft = numSamples;

			/* Make sure the readHead is still good */
			if (_sampleIdx < _introBufSize) {
				_readHead = _introBuf + _sampleIdx;
				
				/* Read from introBuf */
				unsigned int to_read = numSamples;
				if ((_readHead + to_read) > (_introBuf + _introBufSize)) {
					to_read = _introBufSize - (_readHead - _introBuf);
				}
				memcpy(buf, _readHead, to_read * sizeof(SampleType));
				numSamplesLeft -= to_read;
				_readHead += to_read;
				buf += to_read;
				_sampleIdx += to_read;
			}
			
			/* At this point we have either served the whole read
			 *  or we have exhausted the introBuf.
			 */
			
			/* Read from the cache */
			while (numSamplesLeft > 0) {
				int fileBlock = ((_sampleIdx - _introBufSize) / SamplesPerBlock);
				int fileBlockOffset = ((_sampleIdx - _introBufSize) % SamplesPerBlock);
				int ringBufBlock = -1;
				/* Find the new position for the read head */
				for (int i = 0; i < NumBlocks; i++) {
					int block = _bufBlockMap[i];
					if (block == fileBlock) {
						_readHead = _ringBuf + (i * SamplesPerBlock) + fileBlockOffset;
						ringBufBlock = i;
						fileBlock = block;
						break;
					}
				}

				/* Bail if we didn't have the required block in the cache */
				if (ringBufBlock == -1) break;

				/* Clip the memcpy to the end of this block */
				unsigned int to_read = numSamplesLeft;
				SampleType* nextBlockStart = _ringBuf + (ringBufBlock + 1) * SamplesPerBlock;
				if ( nextBlockStart - _readHead <= to_read ) {
					to_read = nextBlockStart - _readHead;
				}
				memcpy(buf, _readHead, to_read * sizeof(SampleType));
				numSamplesLeft -= to_read;
				_readHead += to_read;
				buf += to_read;
				_sampleIdx += to_read;
			}

			return numSamples - numSamplesLeft;
		}

		/* prime() MUST NOT block the read method, which will be
		 *	called from a real-time context like an interrupt handler.
		 */
		bool prime() {
			/* Find any block that could be loaded with a block that is closer
			 * to the read head and load that
			 */

			long int readHeadBlock = 0;
	  
			if (_sampleIdx >= _introBufSize) {
				readHeadBlock = ((_sampleIdx - _introBufSize) / SamplesPerBlock);
			}
	  
			/* First find the furthest block from the read head */
			int idx = 0;
			long maxDiff = labs(_bufBlockMap[0] - readHeadBlock);
			for (int i = 1; i < NumBlocks; i++) {
				long thisDiff = labs(_bufBlockMap[i] - readHeadBlock);
				if (thisDiff > maxDiff) {
					idx = i;
					maxDiff = thisDiff;
				}
			}
     
			/* Determine if it could be filled with something better */
			if (maxDiff > 0) {
        //Serial.println("maxDiff = " + String(maxDiff));
				for (int absDiff = 0; absDiff < maxDiff; absDiff++) {
					long int offset = absDiff;
					/* Look ahead and behind, in that order */
					for (int sign = 0; sign < 2; offset = -offset, sign++) {
						long int block = readHeadBlock + offset;
						if (block < 0) break;
						if (block * SamplesPerBlock > (_file.numSamples() - _introBufSize)) break;
						/* Scan the blocks to see if we already have it
						 *	in the buffer
						 */
						for (int i = 0; i < NumBlocks; i++) {
							if (_bufBlockMap[i] == block) {
								block = -1;
								break;
							}
						}

						if (block >= 0) {
							/* We found a block that is closer to the read head.
							 * Read this block and bail.
							 */
              //Serial.println("Reading block " + String(block) + " to [" + String(idx) + "]");
							if (!_file.seek((block * SamplesPerBlock) + _introBufSize)) {
								break;
							}
              
							size_t numRead = _file.read(_ringBuf + (idx * SamplesPerBlock),
																					BlockSize);
							if(numRead > 0) {
								_bufBlockMap[idx] = block;
								return true;
							}
							/* Handle a short read here */
						}
					}
				}
		  
			}
			return false;
		}

		bool atEOF() {
			return _sampleIdx >= _file.numSamples();
		};

		void setSampleIndex(uint32_t sampleIdx) {
			_sampleIdx = std::min(sampleIdx, _file.numSamples());
		}

		uint32_t sampleIndex() { return _sampleIdx; }

		void reset() {
			setSampleIndex(0);
		}
	
	private:
		
		AudioSamplerError loadIntroBuffer() {
			_file.seek(0); // in samples
			// Find the offset that will put the rest of the buffer on block
			// boundaries
			size_t fileOffset = _file.filePositionForSample(0);
			
			size_t introBufSize = 0; // In bytes

      
			if (fileOffset < BlockSize) {
				introBufSize = (BlockSize - fileOffset) + BlockSize;
			}
			else if (fileOffset % BlockSize != 0) {
				// Next even block - current offset
				introBufSize = (((fileOffset / BlockSize) * BlockSize) + BlockSize) - fileOffset;
				introBufSize += BlockSize;
			}
      else {
        introBufSize = IntroBufCapacity * sizeof(SampleType);
      }
			
			size_t numRead = _file.read(_introBuf, static_cast<uint32_t>(introBufSize));
			if (numRead > 0) {
				_introBufSize = numRead / sizeof(SampleType);
			}
			else {
			  return AudioSamplerError::BadFile;
			}
			return AudioSamplerError::NoErr;
		}

	
		static constexpr int IntroBufCapacity = (BlockSize * 2) / sizeof(SampleType);
		static constexpr int CacheBufSize = (BlockSize * NumBlocks) / sizeof(SampleType);
		static constexpr auto UnmappedBlock = INT_MAX;

		/* Current offset in the buffer */
		SampleType* _readHead;

		/* Position in the file */
		uint32_t	_sampleIdx;

		WavLoader _file;
		
		/* Uses what I am calling a 'P-Buffer'
		 * Which is a ring-buffer with a lead-in
		 *	that optimizes for low-latency sample restarts.
		 */
		size_t _introBufSize;
		SampleType	_introBuf[IntroBufCapacity];
		SampleType	_ringBuf [CacheBufSize];
		unsigned int _bufBlockMap[NumBlocks];

	};
											  
} // namespace Unsaturated

#endif
