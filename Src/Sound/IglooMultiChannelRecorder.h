/**
 ** REXABYTE Modular Engine SDK (Audio DSP / Camera / Input)
 ** Original standalone component developed from scratch by REXABYTE SOFTWARE INC.
 ** Copyright (C) 2026 REXABYTE SOFTWARE INC. All Rights Reserved.
 **
 ** Dual-Licensed Component (Commercial License / GPLv3 with Section 7 Additional Terms):
 **
 **   1. Commercial / Proprietary License (Paid License Required for Commercial Use):
 **      This file is the exclusive intellectual property of REXABYTE SOFTWARE INC.
 **      Any use, copying, modification, integration, or distribution of this file (or
 **      derivative works thereof) in any closed-source product, proprietary software,
 **      paid application, commercial arcade cabinet/hardware, or any distribution that
 **      does not disclose full corresponding source code under GPLv3 Strictly Requires
 **      a prior written, paid Commercial License from REXABYTE SOFTWARE INC.
 **      (authorized for official REXABYTE SOFTWARE INC. commercial products including
 **      the Android edition).
 **
 **   2. Open-Source GPLv3 Integration Grant (Non-Proprietary / Full Copyleft Only):
 **      When distributed strictly as part of a complete open-source GPLv3 project
 **      (such as the PC build of Model 3-Vulkan), REXABYTE SOFTWARE INC. grants permission
 **      to use and distribute this file under the GNU General Public License v3.0 SUBJECT
 **      TO the mandatory Section 7 Additional Terms (see LICENSE-ADDITIONAL-TERMS.md):
 **      all REXABYTE SOFTWARE INC. copyright notices, license headers, and in-app author
 **      attributions MUST be preserved intact. Any removal of attribution or closed-source
 **      commercial exploitation immediately terminates this open-source grant.
 **
 ** This file contains zero code, types, or headers from upstream Supermodel.
 **/

#ifndef IGLOO_MULTICHANNEL_RECORDER_H
#define IGLOO_MULTICHANNEL_RECORDER_H

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace Igloo {
namespace Audio {

using LogCallback = void(*)(bool isError, const char* msg);

#pragma pack(push, 1)
struct WaveFormatExtensibleHeader
{
    // RIFF chunk descriptor (12 bytes)
    char     riffId[4];            // "RIFF"
    uint32_t riffSize;             // 60 + dataSize
    char     waveId[4];            // "WAVE"

    // fmt sub-chunk (8 + 40 = 48 bytes)
    char     fmtId[4];             // "fmt "
    uint32_t fmtSize;              // 40 (sizeof WAVEFORMATEXTENSIBLE)
    uint16_t formatTag;            // 0xFFFE (WAVE_FORMAT_EXTENSIBLE)
    uint16_t channels;             // 1, 2, 4, or 6
    uint32_t sampleRate;           // 44100
    uint32_t avgBytesPerSec;       // sampleRate * blockAlign
    uint16_t blockAlign;           // channels * (bitsPerSample / 8)
    uint16_t bitsPerSample;        // 32 (IEEE Float) or 16 (PCM16)
    uint16_t cbSize;               // 22 (extension size)
    uint16_t validBitsPerSample;   // 32 or 16
    uint32_t channelMask;          // SPEAKER_* mask: 0x33 (4.0 Quad), 0x3F (5.1 Surround)
    uint8_t  subFormat[16];        // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT or KSDATAFORMAT_SUBTYPE_PCM

    // data sub-chunk header (8 bytes)
    char     dataId[4];            // "data"
    uint32_t dataSize;             // payload bytes written
};
#pragma pack(pop)
static_assert(sizeof(WaveFormatExtensibleHeader) == 68, "WAVE_FORMAT_EXTENSIBLE header must be 68 bytes");

inline std::string SanitizeGameTitleForFileName(const std::string& rawTitle)
{
    std::string out;
    out.reserve(rawTitle.size());
    for (size_t i = 0; i < rawTitle.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(rawTitle[i]);
        if (c < 32)
            continue;
        if (c == ':')
        {
            if (!out.empty() && out.back() != ' ')
                out.push_back(' ');
            out.push_back('-');
            continue;
        }
        if (c == '/' || c == '\\' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
        {
            out.push_back('_');
            continue;
        }
        out.push_back(static_cast<char>(c));
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    while (!out.empty() && out.front() == ' ')
        out.erase(out.begin());
    return out.empty() ? std::string("Audio") : out;
}

inline std::string BuildTimestampedRecordingFileName(
    const std::string& rawGameTitle,
    int channels,
    const char* extensionWithDot)
{
    std::time_t now = std::time(nullptr);
    std::tm ltm{};
#if defined(_WIN32)
    localtime_s(&ltm, &now);
#else
    localtime_r(&now, &ltm);
#endif
    static const char* const kWeekdayAbbr[7] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    const char* const weekday = (ltm.tm_wday >= 0 && ltm.tm_wday < 7)
        ? kWeekdayAbbr[ltm.tm_wday] : "";
    const std::string safeTitle = SanitizeGameTitleForFileName(rawGameTitle);

    char timeBuf[96];
    std::snprintf(timeBuf, sizeof(timeBuf), "_%04d-%02d-%02d%s_%02d-%02d-%02d_%dch%s",
        1900 + ltm.tm_year,
        1 + ltm.tm_mon,
        ltm.tm_mday,
        weekday,
        ltm.tm_hour,
        ltm.tm_min,
        ltm.tm_sec,
        channels,
        extensionWithDot ? extensionWithDot : ".wav");

    return safeTitle + timeBuf;
}

/*
 * MultiChannelRecorder<SampleT>
 *
 * Lock-free Single-Producer Single-Consumer (SPSC) asynchronous WAVE_FORMAT_EXTENSIBLE and
 * RFC 9639 lossless multi-channel FLAC recorder. The real-time audio callback thread only pushes
 * interleaved samples into a power-of-two lock-free ring buffer and wakes the background disk
 * writer thread without taking any mutex or performing blocking file I/O.
 */
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324) // structure was padded due to alignment specifier (cache-line SPSC atomics)
#endif

template <typename SampleT = float>
class MultiChannelRecorder
{
public:
    enum class ContainerFormat
    {
        WavFloat32, // WAVE_FORMAT_EXTENSIBLE 32-bit IEEE Float
        WavPcm16,   // WAVE_FORMAT_EXTENSIBLE 16-bit PCM
        FlacPcm24,  // FLAC 24-bit Lossless PCM
        FlacPcm16   // FLAC 16-bit Lossless PCM
    };

    // Speaker position bitmasks per Microsoft WAVEFORMATEXTENSIBLE / USB Audio specification
    static constexpr uint32_t kSpeakerFrontLeft    = 0x00000001u;
    static constexpr uint32_t kSpeakerFrontRight   = 0x00000002u;
    static constexpr uint32_t kSpeakerFrontCenter  = 0x00000004u;
    static constexpr uint32_t kSpeakerLowFrequency = 0x00000008u;
    static constexpr uint32_t kSpeakerBackLeft     = 0x00000010u;
    static constexpr uint32_t kSpeakerBackRight    = 0x00000020u;

    static constexpr uint32_t kMaskMono       = kSpeakerFrontCenter; // 0x04
    static constexpr uint32_t kMaskStereo     = kSpeakerFrontLeft | kSpeakerFrontRight; // 0x03
    static constexpr uint32_t kMaskQuad40     = kSpeakerFrontLeft | kSpeakerFrontRight | kSpeakerBackLeft | kSpeakerBackRight; // 0x33
    static constexpr uint32_t kMaskSurround51 = kSpeakerFrontLeft | kSpeakerFrontRight | kSpeakerFrontCenter |
                                                kSpeakerLowFrequency | kSpeakerBackLeft | kSpeakerBackRight; // 0x3F

    // 2^20 SampleT slots (~4 MB for float32, ~3.96 seconds at 44.1 kHz 6-ch)
    static constexpr size_t kRingCapacity = 1u << 20;
    static constexpr size_t kRingMask     = kRingCapacity - 1;
    static constexpr size_t kFlacBlockSamples = 4096;

    ~MultiChannelRecorder()
    {
        Stop();
    }

    void SetLogCallback(LogCallback cb) noexcept
    {
        m_logCallback = cb;
    }

    static ContainerFormat ParseFormatString(const std::string& str) noexcept
    {
        if (str == "wav_pcm16" || str == "pcm16" || str == "16")
            return ContainerFormat::WavPcm16;
        if (str == "flac_pcm24" || str == "flac24" || str == "flac")
            return ContainerFormat::FlacPcm24;
        if (str == "flac_pcm16" || str == "flac16")
            return ContainerFormat::FlacPcm16;
        return ContainerFormat::WavFloat32;
    }

    static bool IsFlacFormat(ContainerFormat fmt) noexcept
    {
        return fmt == ContainerFormat::FlacPcm24 || fmt == ContainerFormat::FlacPcm16;
    }

    static const char* FormatDescription(ContainerFormat fmt) noexcept
    {
        switch (fmt)
        {
        case ContainerFormat::WavFloat32: return "WAV 32-bit IEEE Float";
        case ContainerFormat::WavPcm16:   return "WAV 16-bit PCM";
        case ContainerFormat::FlacPcm24:  return "FLAC 24-bit PCM";
        case ContainerFormat::FlacPcm16:  return "FLAC 16-bit PCM";
        }
        return "WAV 32-bit IEEE Float";
    }

    bool Start(const std::string& filePath, uint32_t sampleRate, uint16_t channels, ContainerFormat format, LogCallback logCb = nullptr)
    {
        Stop();
        if (logCb != nullptr)
            m_logCallback = logCb;

        m_file = std::fopen(filePath.c_str(), "wb");
        if (!m_file)
        {
            EmitLog(true, "Audio Recorder: unable to open '%s' for writing.", filePath.c_str());
            return false;
        }

        m_filePath = filePath;
        m_sampleRate = sampleRate;
        m_channels = channels;
        m_format = format;
        m_dataBytesWritten = 0;
        m_totalFramesWritten = 0;
        m_flacFrameNumber = 0;
        m_flacMinFrameBytes = 0;
        m_flacMaxFrameBytes = 0;
        m_flacAccum.clear();
        m_droppedSamples.store(0, std::memory_order_relaxed);
        m_readPos.store(0, std::memory_order_relaxed);
        m_writePos.store(0, std::memory_order_relaxed);
        m_ring.assign(kRingCapacity, static_cast<SampleT>(0));

        if (IsFlacFormat(m_format))
            WriteFlacInitialHeaders();
        else
            WriteWavHeader(0);

        m_running.store(true, std::memory_order_release);
        m_writerThread = std::thread(&MultiChannelRecorder::WriterThreadMain, this);

        EmitLog(false, "Audio Recorder: recording %u-channel %s (%u Hz, mask=0x%02X) to '%s'.",
            static_cast<unsigned>(m_channels), FormatDescription(m_format),
            m_sampleRate, ChannelMaskFor(m_channels), m_filePath.c_str());
        return true;
    }

    void Stop()
    {
        if (!m_running.exchange(false, std::memory_order_acq_rel))
            return;

        m_wakeCv.notify_all();
        if (m_writerThread.joinable())
            m_writerThread.join();

        if (m_file)
        {
            if (IsFlacFormat(m_format))
            {
                if (!m_flacAccum.empty() && m_channels > 0)
                {
                    const size_t remFrames = m_flacAccum.size() / m_channels;
                    if (remFrames > 0)
                        EncodeFlacBlock(m_flacAccum.data(), remFrames);
                    m_flacAccum.clear();
                }
                UpdateFlacStreamInfo();
            }
            else
            {
                const uint32_t clampedBytes = (m_dataBytesWritten > 0xFFFFFF00ull)
                    ? 0xFFFFFF00u : static_cast<uint32_t>(m_dataBytesWritten);
                WriteWavHeader(clampedBytes);
            }
            std::fflush(m_file);
            std::fclose(m_file);
            m_file = nullptr;

            const uint64_t dropped = m_droppedSamples.load(std::memory_order_relaxed);
            EmitLog(false, "Audio Recorder: closed '%s' (%.2f MB written, %llu frame(s), %llu dropped sample(s)).",
                m_filePath.c_str(), static_cast<double>(m_dataBytesWritten) / (1024.0 * 1024.0),
                static_cast<unsigned long long>(m_totalFramesWritten), static_cast<unsigned long long>(dropped));
        }
        m_ring.clear();
        m_ring.shrink_to_fit();
        m_flacAccum.clear();
        m_flacAccum.shrink_to_fit();
    }

    // Called on the real-time audio callback thread: strictly lock-free, zero blocking I/O.
    void PushFromAudioCallback(const SampleT* samples, size_t sampleCount) noexcept
    {
        if (!m_running.load(std::memory_order_relaxed) || sampleCount == 0 || samples == nullptr)
            return;

        const size_t writePos = m_writePos.load(std::memory_order_relaxed);
        const size_t readPos  = m_readPos.load(std::memory_order_acquire);
        const size_t used     = writePos - readPos;
        const size_t avail    = (used < kRingCapacity) ? (kRingCapacity - used) : 0;

        if (sampleCount > avail)
        {
            m_droppedSamples.fetch_add(sampleCount - avail, std::memory_order_relaxed);
            sampleCount = avail;
            if (sampleCount == 0)
            {
                m_wakeCv.notify_one();
                return;
            }
        }

        const size_t pos = writePos & kRingMask;
        const size_t firstChunk = std::min(sampleCount, kRingCapacity - pos);
        std::memcpy(&m_ring[pos], samples, firstChunk * sizeof(SampleT));
        if (sampleCount > firstChunk)
            std::memcpy(&m_ring[0], samples + firstChunk, (sampleCount - firstChunk) * sizeof(SampleT));

        m_writePos.store(writePos + sampleCount, std::memory_order_release);
        m_wakeCv.notify_one();
    }

    bool IsRecording() const noexcept
    {
        return m_running.load(std::memory_order_relaxed);
    }

private:
    template <typename... Args>
    void EmitLog(bool isError, const char* fmt, Args... args) const noexcept
    {
        if (!m_logCallback)
            return;
        char buf[1024];
        std::snprintf(buf, sizeof(buf), fmt, args...);
        m_logCallback(isError, buf);
    }

    static uint32_t ChannelMaskFor(uint16_t channels) noexcept
    {
        switch (channels)
        {
        case 1:  return kMaskMono;
        case 2:  return kMaskStereo;
        case 4:  return kMaskQuad40;
        case 6:  return kMaskSurround51;
        default: return 0u;
        }
    }

    static float SampleToNormalizedFloat(SampleT s) noexcept
    {
        if constexpr (std::is_floating_point<SampleT>::value)
        {
            return std::max(-1.0f, std::min(1.0f, static_cast<float>(s)));
        }
        else
        {
            return std::max(-1.0f, std::min(1.0f, static_cast<float>(s) * (1.0f / 32768.0f)));
        }
    }

    void WriteWavHeader(uint32_t dataBytes)
    {
        if (!m_file)
            return;

        const bool useFloat32 = (m_format == ContainerFormat::WavFloat32);
        const uint16_t bitsPerSample = useFloat32 ? 32u : 16u;
        const uint16_t blockAlign = static_cast<uint16_t>(m_channels * (bitsPerSample / 8u));

        WaveFormatExtensibleHeader hdr{};
        std::memcpy(hdr.riffId, "RIFF", 4);
        hdr.riffSize = 60u + dataBytes;
        std::memcpy(hdr.waveId, "WAVE", 4);
        std::memcpy(hdr.fmtId,  "fmt ", 4);
        hdr.fmtSize = 40u;
        hdr.formatTag = 0xFFFEu; // WAVE_FORMAT_EXTENSIBLE
        hdr.channels = m_channels;
        hdr.sampleRate = m_sampleRate;
        hdr.avgBytesPerSec = m_sampleRate * static_cast<uint32_t>(blockAlign);
        hdr.blockAlign = blockAlign;
        hdr.bitsPerSample = bitsPerSample;
        hdr.cbSize = 22u;
        hdr.validBitsPerSample = bitsPerSample;
        hdr.channelMask = ChannelMaskFor(m_channels);

        static const uint8_t kGuidFloat[16] = {
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
            0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
        };
        static const uint8_t kGuidPcm[16] = {
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
            0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
        };
        std::memcpy(hdr.subFormat, useFloat32 ? kGuidFloat : kGuidPcm, 16);

        std::memcpy(hdr.dataId, "data", 4);
        hdr.dataSize = dataBytes;

        std::fseek(m_file, 0, SEEK_SET);
        std::fwrite(&hdr, sizeof(hdr), 1, m_file);
        std::fseek(m_file, 0, SEEK_END);
    }

    // ---- Built-in RFC 9639 Multi-Channel Lossless FLAC Encoder ------------------
    struct BitWriter
    {
        std::vector<uint8_t> bytes;
        uint64_t bitBuf = 0;
        int bitCount = 0;

        void Clear() noexcept
        {
            bytes.clear();
            bitBuf = 0;
            bitCount = 0;
        }

        void PutBits(uint64_t val, int n)
        {
            if (n <= 0)
                return;
            if (n < 64)
                val &= ((1ull << n) - 1ull);
            bitBuf = (bitBuf << n) | val;
            bitCount += n;
            while (bitCount >= 8)
            {
                bitCount -= 8;
                bytes.push_back(static_cast<uint8_t>((bitBuf >> bitCount) & 0xFFu));
            }
        }

        void PutSignedBits(int32_t val, int n)
        {
            const uint64_t u = static_cast<uint64_t>(static_cast<int64_t>(val)) & ((1ull << n) - 1ull);
            PutBits(u, n);
        }

        void PutUnaryZeros(uint32_t zeros)
        {
            while (zeros >= 32)
            {
                PutBits(0, 32);
                zeros -= 32;
            }
            if (zeros > 0)
                PutBits(0, static_cast<int>(zeros));
            PutBits(1, 1);
        }

        void AlignToByte()
        {
            if (bitCount > 0)
            {
                const int pad = 8 - bitCount;
                PutBits(0, pad);
            }
        }
    };

    static uint8_t ComputeCrc8(const uint8_t* data, size_t len) noexcept
    {
        uint8_t crc = 0;
        for (size_t i = 0; i < len; ++i)
        {
            crc ^= data[i];
            for (int b = 0; b < 8; ++b)
                crc = (crc & 0x80u) ? static_cast<uint8_t>((crc << 1) ^ 0x07u) : static_cast<uint8_t>(crc << 1);
        }
        return crc;
    }

    static uint16_t ComputeCrc16(const uint8_t* data, size_t len) noexcept
    {
        uint16_t crc = 0;
        for (size_t i = 0; i < len; ++i)
        {
            crc ^= static_cast<uint16_t>(data[i]) << 8;
            for (int b = 0; b < 8; ++b)
                crc = (crc & 0x8000u) ? static_cast<uint16_t>((crc << 1) ^ 0x8005u) : static_cast<uint16_t>(crc << 1);
        }
        return crc;
    }

    static void WriteUtf8FrameNumber(BitWriter& bw, uint32_t num)
    {
        if (num < 0x80u)
        {
            bw.PutBits(num, 8);
        }
        else if (num < 0x800u)
        {
            bw.PutBits(0xC0u | (num >> 6), 8);
            bw.PutBits(0x80u | (num & 0x3Fu), 8);
        }
        else if (num < 0x10000u)
        {
            bw.PutBits(0xE0u | (num >> 12), 8);
            bw.PutBits(0x80u | ((num >> 6) & 0x3Fu), 8);
            bw.PutBits(0x80u | (num & 0x3Fu), 8);
        }
        else if (num < 0x200000u)
        {
            bw.PutBits(0xF0u | (num >> 18), 8);
            bw.PutBits(0x80u | ((num >> 12) & 0x3Fu), 8);
            bw.PutBits(0x80u | ((num >> 6) & 0x3Fu), 8);
            bw.PutBits(0x80u | (num & 0x3Fu), 8);
        }
        else
        {
            bw.PutBits(0xF8u | (num >> 24), 8);
            bw.PutBits(0x80u | ((num >> 18) & 0x3Fu), 8);
            bw.PutBits(0x80u | ((num >> 12) & 0x3Fu), 8);
            bw.PutBits(0x80u | ((num >> 6) & 0x3Fu), 8);
            bw.PutBits(0x80u | (num & 0x3Fu), 8);
        }
    }

    void WriteFlacStreamInfoBlock(uint64_t totalSamples, uint32_t minFrameBytes, uint32_t maxFrameBytes)
    {
        const uint32_t bps = (m_format == ContainerFormat::FlacPcm24) ? 24u : 16u;
        BitWriter bw;
        // Metadata block header: isLast = 0 (VORBIS_COMMENT follows), type = 0 (STREAMINFO), length = 34
        bw.PutBits(0, 1);
        bw.PutBits(0, 7);
        bw.PutBits(34, 24);

        // STREAMINFO payload (34 bytes)
        bw.PutBits(kFlacBlockSamples, 16); // min block size
        bw.PutBits(kFlacBlockSamples, 16); // max block size
        bw.PutBits(minFrameBytes, 24);     // min frame size
        bw.PutBits(maxFrameBytes, 24);     // max frame size
        bw.PutBits(m_sampleRate, 20);      // sample rate (44100)
        bw.PutBits(m_channels - 1u, 3);    // channels - 1
        bw.PutBits(bps - 1u, 5);           // bits per sample - 1
        bw.PutBits(totalSamples, 36);      // total samples (frames)
        for (int i = 0; i < 16; ++i)
            bw.PutBits(0, 8);              // 128-bit MD5 (0 = unset per RFC 9639)

        std::fwrite(bw.bytes.data(), 1, bw.bytes.size(), m_file);
    }

    void WriteFlacInitialHeaders()
    {
        if (!m_file)
            return;

        std::fseek(m_file, 0, SEEK_SET);
        std::fwrite("fLaC", 1, 4, m_file);
        WriteFlacStreamInfoBlock(0, 0, 0);

        // VORBIS_COMMENT block (isLast = 1, type = 4) carrying WAVEFORMATEXTENSIBLE_CHANNEL_MASK
        const std::string vendor = "REXABYTE SOFTWARE INC.";
        char maskComment[64];
        std::snprintf(maskComment, sizeof(maskComment),
            "WAVEFORMATEXTENSIBLE_CHANNEL_MASK=0x%04X", ChannelMaskFor(m_channels));
        const std::string commentStr(maskComment);

        std::vector<uint8_t> vc;
        auto pushLe32 = [&](uint32_t v) {
            vc.push_back(static_cast<uint8_t>(v & 0xFFu));
            vc.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
            vc.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
            vc.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
        };
        pushLe32(static_cast<uint32_t>(vendor.size()));
        vc.insert(vc.end(), vendor.begin(), vendor.end());
        pushLe32(1u); // 1 user comment
        pushLe32(static_cast<uint32_t>(commentStr.size()));
        vc.insert(vc.end(), commentStr.begin(), commentStr.end());

        uint8_t vcHdr[4] = {
            static_cast<uint8_t>(0x80u | 4u), // isLast = 1, type = 4 (VORBIS_COMMENT)
            static_cast<uint8_t>((vc.size() >> 16) & 0xFFu),
            static_cast<uint8_t>((vc.size() >> 8) & 0xFFu),
            static_cast<uint8_t>(vc.size() & 0xFFu)
        };
        std::fwrite(vcHdr, 1, 4, m_file);
        std::fwrite(vc.data(), 1, vc.size(), m_file);

        m_dataBytesWritten = 4 + 38 + 4 + vc.size();
    }

    void UpdateFlacStreamInfo()
    {
        if (!m_file)
            return;
        std::fseek(m_file, 4, SEEK_SET);
        WriteFlacStreamInfoBlock(m_totalFramesWritten, m_flacMinFrameBytes, m_flacMaxFrameBytes);
        std::fseek(m_file, 0, SEEK_END);
    }

    void EncodeFlacBlock(const SampleT* interleaved, size_t blockFrames)
    {
        if (!m_file || blockFrames == 0 || m_channels == 0)
            return;

        const int bps = (m_format == ContainerFormat::FlacPcm24) ? 24 : 16;
        const float scale = (bps == 24) ? 8388607.0f : 32767.0f;
        const int32_t minVal = (bps == 24) ? -8388607 : -32767;
        const int32_t maxVal = (bps == 24) ?  8388607 :  32767;

        m_frameBw.Clear();

        // Frame header: 14-bit sync code (11111111111110b) + 1-bit reserved (0) + 1-bit blocking strategy (0 = fixed-blocksize) = 0xFFF8 (16 bits)
        m_frameBw.PutBits(0xFFF8u, 16);
        m_frameBw.PutBits(0x07u, 4);    // block size code: 0111 = 16-bit (blocksize - 1) at end of header
        m_frameBw.PutBits(0x09u, 4);    // sample rate code: 1001 = 44.1 kHz
        m_frameBw.PutBits(m_channels - 1u, 4); // channel assignment: 0..7 = 1..8 independent channels
        m_frameBw.PutBits((bps == 24) ? 0x06u : 0x04u, 3); // sample size code: 100=16-bit, 110=24-bit
        m_frameBw.PutBits(0, 1);        // reserved (0)

        WriteUtf8FrameNumber(m_frameBw, m_flacFrameNumber++);
        m_frameBw.PutBits(blockFrames - 1u, 16); // explicit 16-bit (blocksize - 1)

        const uint8_t crc8 = ComputeCrc8(m_frameBw.bytes.data(), m_frameBw.bytes.size());
        m_frameBw.PutBits(crc8, 8);

        // Encode each channel subframe
        m_chanSamples.resize(blockFrames);
        m_residuals.resize(blockFrames);

        for (uint16_t ch = 0; ch < m_channels; ++ch)
        {
            bool allConstant = true;
            int32_t firstSample = 0;
            uint64_t absResidualSum = 0;

            for (size_t i = 0; i < blockFrames; ++i)
            {
                const float norm = SampleToNormalizedFloat(interleaved[i * m_channels + ch]);
                int32_t s = static_cast<int32_t>(std::lrintf(norm * scale));
                s = std::max(minVal, std::min(maxVal, s));
                m_chanSamples[i] = s;
                if (i == 0)
                {
                    firstSample = s;
                }
                else
                {
                    if (s != firstSample)
                        allConstant = false;
                    const int64_t r = static_cast<int64_t>(s) - static_cast<int64_t>(m_chanSamples[i - 1]);
                    const uint64_t u = (r >= 0) ? (static_cast<uint64_t>(r) << 1)
                                                : ((static_cast<uint64_t>(-r) << 1) - 1ull);
                    m_residuals[i] = u;
                    absResidualSum += u;
                }
            }

            if (allConstant)
            {
                // SUBFRAME_CONSTANT: 0 | 000000 | 0
                m_frameBw.PutBits(0, 8);
                m_frameBw.PutSignedBits(firstSample, bps);
                continue;
            }

            // Estimate optimal 4-bit Rice parameter k (0..14) for FIXED order=1
            int bestK = 0;
            if (blockFrames > 1)
            {
                const uint64_t meanU = absResidualSum / (blockFrames - 1);
                while (bestK < 14 && (1ull << bestK) < meanU)
                    ++bestK;
            }

            // Compute exact bit cost for FIXED order=1 with parameter bestK
            uint64_t riceBits = static_cast<uint64_t>(bps) + 6u; // warm-up sample + 2-bit method + 4-bit order + 4-bit k
            riceBits += 4u;
            const uint64_t verbatimBits = static_cast<uint64_t>(blockFrames) * static_cast<uint64_t>(bps);
            for (size_t i = 1; i < blockFrames; ++i)
            {
                const uint64_t q = m_residuals[i] >> bestK;
                riceBits += q + 1u + static_cast<uint64_t>(bestK);
                if (riceBits >= verbatimBits)
                    break;
            }

            if (riceBits < verbatimBits)
            {
                // SUBFRAME_FIXED (order = 1): 0 | 001001 | 0 = 0x12
                m_frameBw.PutBits(0x12u, 8);
                m_frameBw.PutSignedBits(m_chanSamples[0], bps); // 1 warm-up sample
                m_frameBw.PutBits(0, 2);                        // coding method 00 (4-bit Rice)
                m_frameBw.PutBits(0, 4);                        // partition order 0
                m_frameBw.PutBits(static_cast<uint64_t>(bestK), 4);

                const uint64_t mask = (bestK > 0) ? ((1ull << bestK) - 1ull) : 0ull;
                for (size_t i = 1; i < blockFrames; ++i)
                {
                    const uint64_t u = m_residuals[i];
                    const uint32_t q = static_cast<uint32_t>(u >> bestK);
                    m_frameBw.PutUnaryZeros(q);
                    if (bestK > 0)
                        m_frameBw.PutBits(u & mask, bestK);
                }
            }
            else
            {
                // SUBFRAME_VERBATIM: 0 | 000001 | 0 = 0x02
                m_frameBw.PutBits(0x02u, 8);
                for (size_t i = 0; i < blockFrames; ++i)
                    m_frameBw.PutSignedBits(m_chanSamples[i], bps);
            }
        }

        m_frameBw.AlignToByte();
        const uint16_t crc16 = ComputeCrc16(m_frameBw.bytes.data(), m_frameBw.bytes.size());
        m_frameBw.PutBits(crc16, 16);

        const size_t written = std::fwrite(m_frameBw.bytes.data(), 1, m_frameBw.bytes.size(), m_file);
        m_dataBytesWritten += written;
        m_totalFramesWritten += blockFrames;

        const uint32_t frameBytes = static_cast<uint32_t>(m_frameBw.bytes.size());
        if (m_flacMinFrameBytes == 0 || frameBytes < m_flacMinFrameBytes)
            m_flacMinFrameBytes = frameBytes;
        if (frameBytes > m_flacMaxFrameBytes)
            m_flacMaxFrameBytes = frameBytes;
    }

    void DrainRingToDisk(std::vector<SampleT>& staging, std::vector<int16_t>& pcm16Buf, std::vector<float>& floatBuf)
    {
        const size_t writePos = m_writePos.load(std::memory_order_acquire);
        size_t readPos        = m_readPos.load(std::memory_order_relaxed);
        const size_t avail    = writePos - readPos;
        if (avail == 0 || !m_file)
            return;

        staging.resize(avail);
        const size_t pos = readPos & kRingMask;
        const size_t firstChunk = std::min(avail, kRingCapacity - pos);
        std::memcpy(staging.data(), &m_ring[pos], firstChunk * sizeof(SampleT));
        if (avail > firstChunk)
            std::memcpy(staging.data() + firstChunk, &m_ring[0], (avail - firstChunk) * sizeof(SampleT));

        m_readPos.store(readPos + avail, std::memory_order_release);

        if (IsFlacFormat(m_format))
        {
            m_flacAccum.insert(m_flacAccum.end(), staging.begin(), staging.end());
            const size_t samplesPerBlock = kFlacBlockSamples * static_cast<size_t>(m_channels);
            size_t offset = 0;
            while (m_flacAccum.size() - offset >= samplesPerBlock)
            {
                EncodeFlacBlock(m_flacAccum.data() + offset, kFlacBlockSamples);
                offset += samplesPerBlock;
            }
            if (offset > 0)
                m_flacAccum.erase(m_flacAccum.begin(), m_flacAccum.begin() + static_cast<std::ptrdiff_t>(offset));
            return;
        }

        if (m_channels > 0)
            m_totalFramesWritten += avail / m_channels;

        if constexpr (std::is_floating_point<SampleT>::value)
        {
            if (m_format == ContainerFormat::WavFloat32)
            {
                const size_t written = std::fwrite(staging.data(), sizeof(float), avail, m_file);
                m_dataBytesWritten += written * sizeof(float);
            }
            else
            {
                pcm16Buf.resize(avail);
                for (size_t i = 0; i < avail; ++i)
                {
                    const float clamped = std::max(-1.0f, std::min(1.0f, static_cast<float>(staging[i])));
                    pcm16Buf[i] = static_cast<int16_t>(std::lrintf(clamped * 32767.0f));
                }
                const size_t written = std::fwrite(pcm16Buf.data(), sizeof(int16_t), avail, m_file);
                m_dataBytesWritten += written * sizeof(int16_t);
            }
        }
        else
        {
            if (m_format != ContainerFormat::WavFloat32)
            {
                const size_t written = std::fwrite(staging.data(), sizeof(int16_t), avail, m_file);
                m_dataBytesWritten += written * sizeof(int16_t);
            }
            else
            {
                floatBuf.resize(avail);
                for (size_t i = 0; i < avail; ++i)
                    floatBuf[i] = static_cast<float>(staging[i]) * (1.0f / 32768.0f);
                const size_t written = std::fwrite(floatBuf.data(), sizeof(float), avail, m_file);
                m_dataBytesWritten += written * sizeof(float);
            }
        }
    }

    void WriterThreadMain()
    {
        std::vector<SampleT> staging;
        std::vector<int16_t> pcm16Buf;
        std::vector<float> floatBuf;
        staging.reserve(32768);

        while (m_running.load(std::memory_order_acquire))
        {
            {
                std::unique_lock<std::mutex> lock(m_wakeMutex);
                m_wakeCv.wait_for(lock, std::chrono::milliseconds(25), [this]() {
                    return !m_running.load(std::memory_order_relaxed) ||
                           (m_writePos.load(std::memory_order_relaxed) != m_readPos.load(std::memory_order_relaxed));
                });
            }
            DrainRingToDisk(staging, pcm16Buf, floatBuf);
        }

        // Flush remaining samples queued before Stop()
        DrainRingToDisk(staging, pcm16Buf, floatBuf);
    }

    std::FILE*              m_file = nullptr;
    std::string             m_filePath;
    uint32_t                m_sampleRate = 44100;
    uint16_t                m_channels = 4;
    ContainerFormat         m_format = ContainerFormat::WavFloat32;
    uint64_t                m_dataBytesWritten = 0;
    uint64_t                m_totalFramesWritten = 0;
    uint32_t                m_flacFrameNumber = 0;
    uint32_t                m_flacMinFrameBytes = 0;
    uint32_t                m_flacMaxFrameBytes = 0;
    LogCallback             m_logCallback = nullptr;

    std::vector<SampleT>    m_ring;
    std::vector<SampleT>    m_flacAccum;
    std::vector<int32_t>    m_chanSamples;
    std::vector<uint64_t>   m_residuals;
    BitWriter               m_frameBw;
    alignas(64) std::atomic<size_t> m_writePos{0};
    alignas(64) std::atomic<size_t> m_readPos{0};
    std::atomic<uint64_t>   m_droppedSamples{0};
    std::atomic<bool>       m_running{false};

    std::mutex              m_wakeMutex;
    std::condition_variable m_wakeCv;
    std::thread             m_writerThread;
};

using MultiChannelWavRecorder = MultiChannelRecorder<float>;

#ifdef _MSC_VER
#pragma warning(pop)
#endif

} // namespace Audio
} // namespace Igloo

#endif // IGLOO_MULTICHANNEL_RECORDER_H
