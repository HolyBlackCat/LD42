#ifndef UTILS_AUDIO_H_INCLUDED
#define UTILS_AUDIO_H_INCLUDED

#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <unordered_set>
#include <utility>

#include <AL/al.h>
#include <AL/alc.h>
#include <vorbis/vorbisfile.h>

#include "program/errors.h"
#include "utils/finally.h"
#include "utils/mat.h"

namespace Audio
{
    class Context // The only context is ref-counted.
    {
        inline static int ref_count = 0;
        inline static ALCdevice *device = 0;
        inline static ALCcontext *context = 0;

      public:
        Context()
        {
            if (ref_count != 0)
                return;
            device = alcOpenDevice(0);
            if (!device)
                Program::HardError("Unable to open OpenAL device.");
            ALCint major, minor;
            alcGetIntegerv(device, ALC_MAJOR_VERSION, 1, &major);
            alcGetIntegerv(device, ALC_MINOR_VERSION, 1, &minor);
            if (major * 1000 + minor < 1001)
            {
                alcCloseDevice(device);
                Program::HardError("OpenAL dynamic library is too old. Expected at least 1.1.");
            }
            context = alcCreateContext(device, 0);
            if (!context)
            {
                alcCloseDevice(device);
                Program::HardError("Unable to create OpenAL context.");
            }
            if (!alcMakeContextCurrent(context))
            {
                alcDestroyContext(context);
                alcCloseDevice(device);
                Program::HardError("Can't enable OpenAL context.");
            }
            ref_count++;
        }
        Context(const Context &) : Context() {}

        ~Context()
        {
            if (--ref_count != 0)
                return;
            if (context)
            {
                alcDestroyContext(context);
                context = 0;
            }
            if (device)
            {
                alcCloseDevice(device);
                device = 0;
            }
        }

        void CheckErrors() const
        {
            switch (alcGetError(device))
            {
                case 0: return;
                case ALC_INVALID_DEVICE:  Program::HardError("OpenAL error: Invalid device.");
                case ALC_INVALID_CONTEXT: Program::HardError("OpenAL error: Invalid context.");
                case ALC_INVALID_ENUM:    Program::HardError("OpenAL error: Invalid enum.");
                case ALC_INVALID_VALUE:   Program::HardError("OpenAL error: Invalid value.");
                case ALC_OUT_OF_MEMORY:   Program::HardError("OpenAL error: Out of memory.");
                default:                  Program::HardError("OpenAL error: Unknown error.");
            }
        }

        static bool Exists()
        {
            return context;
        }
    };

    inline void Volume(float vol)
    {
        alListenerf(AL_GAIN, vol);
    }
    inline void Pitch(float pitch)
    {
        alListenerf(AL_PITCH, pitch);
    }

    inline void ListenerPos(fvec3 v)
	{
		alListenerfv(AL_POSITION, v.as_array());
	}
	inline void ListenerVel(fvec3 v)
	{
		alListenerfv(AL_VELOCITY, v.as_array());
	}
	inline void ListenerRot(fvec3 fwd, fvec3 up)
	{
		fvec3 arr[2] = {fwd, up};
		alListenerfv(AL_ORIENTATION, (float *)arr);
	}

    inline void DopplerFactor(float n)
    {
        alDopplerFactor(n);
    }
	inline void SpeedOfSound(float n)
	{
	    alSpeedOfSound(n);
    }


    class Sound
    {
      public:
        enum Format_t
        {
            mono8    = AL_FORMAT_MONO8,
            mono16   = AL_FORMAT_MONO16,
            stereo8  = AL_FORMAT_STEREO8,
            stereo16 = AL_FORMAT_STEREO16,
        };
      private:
        std::vector<uint8_t> data;
        int freq = 44100;
        Format_t format = mono8;
      public:
        void FromWAV(MemoryFile file)
        {
            if (file.size() < 44) // 44 is the size of WAV header.
                Program::HardError("Unable to parse `", file.name(), "`: The file is too small for a header.");

            const uint8_t *ptr = file.data();

            if (memcmp(ptr, "RIFF", 4)) Program::HardError("Unable to parse `", file.name(), "`: No \"RIFF\" label.");
            ptr += 4;

            uint32_t new_byte_size;
            std::memcpy(&new_byte_size, ptr, 4);
            new_byte_size -= 36;
            if (file.size() < new_byte_size + 44) Program::HardError("Unable to parse `", file.name(), "`: Unexpected end of file.");
            ptr += 4;

            if (memcmp(ptr, "WAVE", 4)) Program::HardError("Unable to parse `", file.name(), "`: No \"WAVE\" label.");
            ptr += 4;

            if (memcmp(ptr, "fmt ", 4)) Program::HardError("Unable to parse `", file.name(), "`: No \"fmt \" label.");
            ptr += 4;

            if (memcmp(ptr, "\x10\0\0", 4)) Program::HardError("Unable to parse `", file.name(), "`: File structure is too complicated.");
            ptr += 4;

            if (memcmp(ptr, "\x1", 2)) Program::HardError("Unable to parse `", file.name(), "`: File is compresssed or uses floating-point samples.");
            ptr += 2;

            uint16_t channels;
            std::memcpy(&channels, ptr, 2);
            if (channels == 0 || channels > 2) Program::HardError("Unable to parse `", file.name(), "`: The file must be mono or stereo.");
            ptr += 2;

            uint32_t new_freq;
            std::memcpy(&new_freq, ptr, 4);
            ptr += 4;

            ptr += 6; // Skip useless data.

            uint16_t bits_per_sample;
            std::memcpy(&bits_per_sample, ptr, 2);
            if (bits_per_sample != 8 && bits_per_sample != 16) Program::HardError("Unable to parse `", file.name(), "`: The file must use 8 or 16 bits per sample.");
            if (bits_per_sample == 16 && new_byte_size % 2 != 0) Program::HardError("Unable to parse `", file.name(), "`: The file uses 16 bits per sample, but data size is not a multiple of two.");
            ptr += 2;

            if (memcmp(ptr, "data", 4)) Program::HardError("Unable to parse `", file.name(), "`: No \"data\" label.");
            ptr += 4;

            uint32_t tmp;
            std::memcpy(&tmp, ptr, 4);
            if (tmp != new_byte_size) Program::HardError("Unable to parse `", file.name(), "`: The file contains additional sectons or is corrupted.");
            ptr += 4;

            Format_t new_format;
            switch (channels << 16 | bits_per_sample)
            {
                default:
                case 1 << 16 |  8: new_format = mono8;    break;
                case 1 << 16 | 16: new_format = mono16;   break;
                case 2 << 16 |  8: new_format = stereo8;  break;
                case 2 << 16 | 16: new_format = stereo16; break;
            }

            std::vector<uint8_t> new_data;
            new_data.reserve(new_byte_size);
            new_data.insert(new_data.end(), ptr, ptr + new_byte_size);

            data = std::move(new_data);
            freq = new_freq;
            format = new_format;
        }
        void FromOGG(MemoryFile file, bool load_as_8bit = 0)
        {
            (void)OV_CALLBACKS_DEFAULT;
            (void)OV_CALLBACKS_NOCLOSE;
            (void)OV_CALLBACKS_STREAMONLY;
            (void)OV_CALLBACKS_STREAMONLY_NOCLOSE;

            struct Desc
            {
                const uint8_t *start, *cur, *end;
            };
            Desc desc{file.data(), file.data(), file.data() + file.size()};

            ov_callbacks callbacks;
            callbacks.tell_func = [](void *ptr) -> long
            {
                Desc &ref = *(Desc *)ptr;
                return ref.cur - ref.start;
            };
            callbacks.seek_func = [](void *ptr, int64_t offset, int mode) -> int
            {
                Desc &ref = *(Desc *)ptr;
                switch (mode)
                {
                  case SEEK_SET:
                    ref.cur = ref.start + offset;
                    break;
                  case SEEK_CUR:
                    ref.cur += offset;
                    break;
                  case SEEK_END:
                    ref.cur = ref.end + offset;
                    break;
                  default:
                    return 1;
                }
                if (ref.cur < ref.start || ref.cur > ref.end)
                    return 1;
                return 0;
            };
            callbacks.read_func = [](void *dst, std::size_t sz, std::size_t count, void *ptr) -> std::size_t
            {
                Desc &ref = *(Desc *)ptr;
                if (ref.cur + count * sz > ref.end)
                    count = (ref.end - ref.cur) / sz;
                std::copy(ref.cur, ref.cur + count, (uint8_t *)dst);
                ref.cur += count * sz;
                return count;
            };
            callbacks.close_func = 0;

            OggVorbis_File ogg_file;
            switch (ov_open_callbacks(&desc, &ogg_file, 0, 0, callbacks))
            {
              case 0:
                break;
              case OV_EREAD:
                Program::HardError("Unable to parse `", file.name(), "`: Unable to read data from the stream.");
                break;
              case OV_ENOTVORBIS:
                Program::HardError("Unable to parse `", file.name(), "`: This is not vorbis audio.");
                break;
              case OV_EVERSION:
                Program::HardError("Unable to parse `", file.name(), "`: Vorbis version mismatch.");
                break;
              case OV_EBADHEADER:
                Program::HardError("Unable to parse `", file.name(), "`: Invalid header.");
                break;
              case OV_EFAULT:
                Program::HardError("Unable to parse `", file.name(), "`: Internal vorbis error.");
                break;
              default:
                Program::HardError("Unable to parse `", file.name(), "`: Unknown vorbis error.");
                break;
            }
            // IMPORTANT: Below this line you must do `ov_clear(&ogg_file);` before returning or throwing anything.

            vorbis_info *info = ov_info(&ogg_file, -1);
            uint64_t samples = ov_pcm_total(&ogg_file, -1);
            if (samples > 0xffffffffu)
            {
                ov_clear(&ogg_file);
                Program::HardError("Unable to parse `", file.name(), "`: The file is too big.");
            }

            Sound new_obj;

            try
            {
                switch (info->channels << 16 | load_as_8bit)
                {
                  case 1 << 16 | 1:
                    new_obj.FromMemory(mono8, info->rate, samples);
                    break;
                  case 1 << 16 | 0:
                    new_obj.FromMemory(mono16, info->rate, samples);
                    break;
                  case 2 << 16 | 1:
                    new_obj.FromMemory(stereo8, info->rate, samples);
                    break;
                  case 2 << 16 | 0:
                    new_obj.FromMemory(stereo16, info->rate, samples);
                    break;
                  default:
                    Program::HardError("Unable to parse `", file.name(), "`: The file must be mono or stereo, but this one has ", info->channels, " channels.");
                    break;
                }
            }
            catch (...)
            {
                ov_clear(&ogg_file);
                throw;
            }

            uint32_t buf_len = new_obj.Bytes();
            char *buf = (char *)new_obj.Data();

            int current_bitstream = -1;
            while (1)
            {
                if (buf_len == 0)
                    break;
                int bitstream;
                long val = ov_read(&ogg_file, buf, buf_len, 0, load_as_8bit ? 1 : 2, !load_as_8bit, &bitstream);
                if (val == 0)
                {
                    ov_clear(&ogg_file);
                    Program::HardError("Unable to parse `", file.name(), "`: Unexpected end of stream.");
                }
                if (bitstream != current_bitstream)
                {
                    current_bitstream = bitstream;
                    vorbis_info *local_info = ov_info(&ogg_file, bitstream);
                    if (local_info->channels != info->channels)
                    {
                        ov_clear(&ogg_file);
                        Program::HardError("Unable to parse `", file.name(), "`: The amount of channels has changed from ", info->channels, " to ", local_info->channels, ". Dynamic amount of channels is not supported.");
                    }
                    if (local_info->rate != info->rate)
                    {
                        ov_clear(&ogg_file);
                        Program::HardError("Unable to parse `", file.name(), "`: The sampling rate has changed from ", info->rate, " to ", local_info->rate, ". Dynamic sampling rate is not supported.");
                    }
                }
                switch (val)
                {
                  case OV_HOLE:
                    ov_clear(&ogg_file);
                    Program::HardError("Unable to parse `", file.name(), "`: The file is corrupted.");
                    break;
                  case OV_EBADLINK:
                    ov_clear(&ogg_file);
                    Program::HardError("Unable to parse `", file.name(), "`: Bad link.");
                    break;
                  case OV_EINVAL:
                    ov_clear(&ogg_file);
                    Program::HardError("Unable to parse `", file.name(), "`: Invalid header.");
                    break;
                  default:
                    buf += val;
                    buf_len -= val;
                    break;
                }
            }

            ov_clear(&ogg_file);

            *this = std::move(new_obj);
        }
        void FromWAV_Mono(MemoryFile file)
        {
            FromWAV(file);
            if (Stereo())
                Program::HardError("Expected `", file.name(), "` to be mono.");
        }
        void FromOGG_Mono(MemoryFile file, bool load_as_8bit = 0)
        {
            FromOGG(file, load_as_8bit);
            if (Stereo())
                Program::HardError("Expected `", file.name(), "` to be mono.");
        }
        void FromWAV_Stereo(MemoryFile file)
        {
            FromWAV(file);
            if (Mono())
                Program::HardError("Expected `", file.name(), "` to be stereo.");
        }
        void FromOGG_Stereo(MemoryFile file, bool load_as_8bit = 0)
        {
            FromOGG(file, load_as_8bit);
            if (Mono())
                Program::HardError("Expected `", file.name(), "` to be stereo.");
        }

        [[nodiscard]] static Sound WAV       (MemoryFile file) {Sound ret; ret.FromWAV       (file); return ret;}
        [[nodiscard]] static Sound WAV_Mono  (MemoryFile file) {Sound ret; ret.FromWAV_Mono  (file); return ret;}
        [[nodiscard]] static Sound WAV_Stereo(MemoryFile file) {Sound ret; ret.FromWAV_Stereo(file); return ret;}
        [[nodiscard]] static Sound OGG       (MemoryFile file, bool load_as_8bit = 0) {Sound ret; ret.FromOGG       (file, load_as_8bit); return ret;}
        [[nodiscard]] static Sound OGG_Mono  (MemoryFile file, bool load_as_8bit = 0) {Sound ret; ret.FromOGG_Mono  (file, load_as_8bit); return ret;}
        [[nodiscard]] static Sound OGG_Stereo(MemoryFile file, bool load_as_8bit = 0) {Sound ret; ret.FromOGG_Stereo(file, load_as_8bit); return ret;}

        void FromMemory(Format_t new_format, int new_freq, uint32_t new_sample_count, const uint8_t *new_data = 0)
        {
            format = new_format;
            freq = new_freq;
            data.resize(new_sample_count * BytesPerSample());
            if (new_data)
                std::copy(new_data, new_data + data.size(), (uint8_t *)data.data());
        }

        int BytesPerSample() const
        {
            switch (format)
            {
                default:
                case mono8:    return 1;
                case mono16:   return 2;
                case stereo8:  return 2;
                case stereo16: return 4;
            }
        }

        void Clear()
        {
            data = {};
        }
        uint8_t *Data()
        {
            return data.data();
        }
        const uint8_t *Data() const
        {
            return data.data();
        }
        int Samples() const
        {
            return data.size() / BytesPerSample();
        }
        uint32_t Bytes() const
        {
            return data.size();
        }
        uint32_t Frequency() const
        {
            return freq;
        }
        Format_t Format() const
        {
            return format;
        }
        bool Mono() const
        {
            return format == Format_t::mono8 || format == Format_t::mono16;
        }
        bool Stereo() const
        {
            return format == Format_t::stereo8 || format == Format_t::stereo16;
        }
        bool Bits8() const
        {
            return format == Format_t::mono8 || format == Format_t::stereo8;
        }
        bool Bits16() const
        {
            return format == Format_t::mono16 || format == Format_t::stereo16;
        }
    };


    class Source;

    class Buffer
    {
        struct Data
        {
            ALuint handle = 0;
        };

        Data data;

      public:
        Buffer()
        {
            alGenBuffers(1, &data.handle);
            if (!data.handle)
                Program::Error("Unable to create AL buffer.");
        }

        Buffer(Buffer &&other) noexcept : data(std::exchange(other.data, {})) {}
        Buffer &operator=(Buffer other) noexcept
        {
            std::swap(data, other.data);
            return *this;
        }

        Buffer(const Sound &sound) : Buffer()
        {
            SetData(sound);
        }

        void SetData(Sound::Format_t format, int freq, int bytes, const uint8_t *ptr = 0)
        {
            alBufferData(data.handle, (ALenum)format, ptr, bytes, freq);
        }
        void SetData(const Sound &data)
        {
            SetData(data.Format(), data.Frequency(), data.Bytes(), data.Data());
        }

        ALuint Handle() const
        {
            return data.handle;
        }

        Source operator()(float volume = 1, float pitch = 1) const; // Creates a temporary source to play the sound.
    };


    class Source
    {
        inline static float default_ref_dist = 1,
                            default_rolloff_fac = 1,
                            default_max_dist = std::numeric_limits<float>::infinity();

        class Object
        {
            ALuint handle;
          public:
            Object()
            {
                alGenSources(1, &handle);
                if (!handle) handle = -1;
                alSourcef(*this, AL_REFERENCE_DISTANCE, default_ref_dist);
                alSourcef(*this, AL_ROLLOFF_FACTOR,     default_rolloff_fac);
                alSourcef(*this, AL_MAX_DISTANCE,       default_max_dist);
            }
            Object(const Object &) = delete;
            Object &operator=(const Object &) = delete;
            ~Object()
            {
                alDeleteSources(1, &handle);
            }

            operator ALuint() const
            {
                return handle;
            }
        };

        std::shared_ptr<Object> object;
        inline static std::unordered_set<std::shared_ptr<Object>> list;

        bool temp = 0;

        using ref = Source &&;

      public:
        Source() {}

        // This deletes copy constructor and assignment.
        Source(Source &&o) : object(std::move(o.object)), temp(o.temp)
        {
            o.temp = 0;
        }
        Source &operator=(Source &&o) = delete;

        ~Source()
        {
            if (Exists() && temp && *object != ALuint(-1))
            {
                int looping;
                alGetSourcei(*object, AL_LOOPING, &looping);
                if (looping)
                    return;
                play();
                list.insert(object);
            }
        }

        static void RemoveUnused()
        {
            auto it = list.begin();

            while (it != list.end())
            {
                int state;
                alGetSourcei(**it, AL_SOURCE_STATE, &state);
                if (state != AL_PLAYING)
                    it = list.erase(it);
                else
                    it++;
            }
        }


        void Create(const Buffer &buffer)
        {
            if (Exists())
                return;
            object = std::make_shared<Object>();
            temp = 0;
            alSourcei(*object, AL_BUFFER, buffer.Handle());
        }
        void Destroy()
        {
            object.reset();
        }
        bool Exists() const
        {
            return bool(object);
        }

        /* The volume curve is a hyperbola, clamped at 1 (if `distance` < `ref`).
         *             /                   1                \
         * volume = min| 1 , ------------------------------ |
         *             \     1 + fac * (distance * ref - 1) /
         * `distance` is clamped at `max`.
         */

        static void DefaultMaxDistance(float d)
        {
            default_max_dist = d;
        }
        static void DefaultRefDistance(float d)
        {
            default_ref_dist = d;
        }
        static void DefaultRolloffFactor(float f)
        {
            default_rolloff_fac = f;
        }


        ref max_distance(float d)
        {
            if (*object == ALuint(-1)) return (ref)*this;
            alSourcef(*object, AL_MAX_DISTANCE, d);
            return (ref)*this;
        }
        ref ref_distance(float d)
        {
            if (*object == ALuint(-1)) return (ref)*this;
            alSourcef(*object, AL_REFERENCE_DISTANCE, d);
            return (ref)*this;
        }
        ref rolloff_factor(float f)
        {
            if (*object == ALuint(-1)) return (ref)*this;
            alSourcef(*object, AL_ROLLOFF_FACTOR, f);
            return (ref)*this;
        }

        ref temporary() // Doesn't work for looped sounds. Plays the sound after the object is destroyed.
        {
            if (*object == ALuint(-1)) return (ref)*this;
            temp = 1;
            return (ref)*this;
        }

        ref volume(float v)
        {
            if (*object == ALuint(-1)) return (ref)*this;
            alSourcef(*object, AL_GAIN, v);
            return (ref)*this;
        }
        ref pitch(float v)
        {
            if (*object == ALuint(-1)) return (ref)*this;
            alSourcef(*object, AL_PITCH, v);
            return (ref)*this;
        }
        ref loop(float l)
        {
            if (*object == ALuint(-1)) return (ref)*this;
            alSourcei(*object, AL_LOOPING, l);
            return (ref)*this;
        }

        ref play()
        {
            if (*object == ALuint(-1)) return (ref)*this;
            alSourcePlay(*object);
            return (ref)*this;
        }
        ref stop()
        {
            if (*object == ALuint(-1)) return (ref)*this;
            alSourceStop(*object);
            return (ref)*this;
        }

        // All functions below do not work for stereo sources.

        ref pos(fvec3 p)
        {
            if (*object == ALuint(-1)) return (ref)*this;
            alSourcefv(*object, AL_POSITION, p.as_array());
            return (ref)*this;
        }
        ref vel(fvec3 v)
        {
            if (*object == ALuint(-1)) return (ref)*this;
            alSourcefv(*object, AL_VELOCITY, v.as_array());
            return (ref)*this;
        }
        ref relative(bool r = 1)
        {
            if (*object == ALuint(-1)) return (ref)*this;
            alSourcei(*object, AL_SOURCE_RELATIVE, r);
            return (ref)*this;
        }

        ref pos(fvec2 p)
        {
            return (ref)pos(p.to_vec3());
        }
        ref vel(fvec2 p)
        {
            return (ref)vel(p.to_vec3());
        }
    };

    inline Source Buffer::operator()(float volume, float pitch) const // Creates a temporary source to play the sound.
    {
        Source src;
        src.Create(*this);
        src.temporary().volume(volume).pitch(pitch);
        return src;
    }
}

#endif

