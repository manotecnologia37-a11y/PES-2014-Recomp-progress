// Based on Blackline Interactive implementation
#include "runtime/ps2_memory.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

enum VIFCmd : uint8_t
{
    VIF_NOP = 0x00,
    VIF_STCYCL = 0x01,
    VIF_OFFSET = 0x02,
    VIF_BASE = 0x03,
    VIF_ITOP = 0x04,
    VIF_STMOD = 0x05,
    VIF_MSKPATH3 = 0x06,
    VIF_MARK = 0x07,
    VIF_FLUSHE = 0x10,
    VIF_FLUSH = 0x11,
    VIF_FLUSHA = 0x13,
    VIF_MSCAL = 0x14,
    VIF_MSCALF = 0x15,
    VIF_MSCNT = 0x17,
    VIF_STMASK = 0x20,
    VIF_STROW = 0x30,
    VIF_STCOL = 0x31,
    VIF_MPG = 0x4A,
    VIF_DIRECT = 0x50,
    VIF_DIRECTHL = 0x51,
};

namespace
{
    constexpr uint8_t kGifFmtImage = 2u;

    bool hasDirectImageCommand(const uint8_t *data, uint32_t sizeBytes)
    {
        // A DMA tag can contribute padding or a flush before the DIRECT code.
        for (uint32_t pos = 0u; pos < 16u && pos + 4u <= sizeBytes; pos += 4u)
        {
            uint32_t command = 0u;
            std::memcpy(&command, data + pos, sizeof(command));
            const uint8_t opcode = static_cast<uint8_t>((command >> 24u) & 0x7fu);
            if (opcode == VIF_DIRECT || opcode == VIF_DIRECTHL)
            {
                const uint32_t qwc = command & 0xffffu;
                return (command & 0x00ff0000u) == 0u && qwc != 0u &&
                       qwc <= (sizeBytes - pos - 4u) / 16u;
            }
            if (command != 0u && opcode != VIF_FLUSHE && opcode != VIF_FLUSH && opcode != VIF_FLUSHA)
                return false;
        }
        return false;
    }

    uint32_t pendingGifImageQwc(const uint8_t *data, uint32_t sizeBytes)
    {
        if (!data || sizeBytes < 16u)
            return 0u;

        uint32_t offset = 0u;
        while (offset + 16u <= sizeBytes)
        {
            uint64_t tagLo = 0u;
            std::memcpy(&tagLo, data + offset, sizeof(tagLo));
            offset += 16u;

            const uint32_t nloop = static_cast<uint32_t>(tagLo & 0x7FFFu);
            const uint8_t flg = static_cast<uint8_t>((tagLo >> 58) & 0x3u);
            uint32_t nreg = static_cast<uint32_t>((tagLo >> 60) & 0xFu);
            if (nreg == 0u)
                nreg = 16u;

            uint64_t payloadBytes = 0u;
            if (flg == 0u) // PACKED
            {
                payloadBytes = static_cast<uint64_t>(nloop) * nreg * 16ull;
            }
            else if (flg == 1u) // REGLIST, padded to a quadword
            {
                payloadBytes = static_cast<uint64_t>(nloop) * nreg * 8ull;
                payloadBytes = (payloadBytes + 15ull) & ~15ull;
            }
            else if (flg == kGifFmtImage || flg == 3u) // FLG=3 ("disabled" in the GS manual) behaves exactly like IMAGE: NLOOP qwords of image data.
            {
                payloadBytes = static_cast<uint64_t>(nloop) * 16ull;
                const uint64_t availableBytes = sizeBytes - offset;
                if (payloadBytes > availableBytes)
                {
                    return static_cast<uint32_t>((payloadBytes - availableBytes) / 16ull);
                }
            }
            else
            {
                return 0u;
            }

            if (payloadBytes > static_cast<uint64_t>(sizeBytes - offset))
                return 0u;
            offset += static_cast<uint32_t>(payloadBytes);
        }

        return 0u;
    }

    // VIF MPG writes into fixed-size VU micro memory: the destination address is
    // masked to the region and payloads that cross the end wrap to address zero
    // (PCSX2 Vif_Codes.cpp does the same).
    void uploadMicroMemory(uint8_t *micro, uint32_t microSize, uint32_t address,
                           const uint8_t *data, uint32_t size)
    {
        const uint32_t firstSize = std::min(size, microSize - address);
        std::memcpy(micro + address, data, firstSize);
        if (firstSize < size)
            std::memcpy(micro, data + firstSize, size - firstSize);
    }
}

void PS2Memory::processVIF0Data(uint32_t srcPhys, uint32_t sizeBytes)
{
    if (sizeBytes == 0u || srcPhys >= PS2_RAM_SIZE)
        return;

    const uint64_t requestedEnd = static_cast<uint64_t>(srcPhys) + static_cast<uint64_t>(sizeBytes);
    if (requestedEnd > static_cast<uint64_t>(PS2_RAM_SIZE))
        sizeBytes = PS2_RAM_SIZE - srcPhys;

    processVIF0Data(m_rdram + srcPhys, sizeBytes);
}

void PS2Memory::processVIF0Data(const uint8_t *data, uint32_t sizeBytes)
{
    if (sizeBytes == 0u)
        return;

    uint32_t pos = 0;
    while (pos + 4 <= sizeBytes)
    {
        uint32_t cmd = 0u;
        std::memcpy(&cmd, data + pos, sizeof(cmd));
        pos += 4u;

        const uint8_t opcode = static_cast<uint8_t>((cmd >> 24) & 0x7Fu);
        const uint16_t imm = static_cast<uint16_t>(cmd & 0xFFFFu);
        const uint8_t num = static_cast<uint8_t>((cmd >> 16) & 0xFFu);
        const bool irq = (cmd & 0x80000000u) != 0u;

        vif0_regs.code = cmd;
        vif0_regs.num = num;
        if (irq)
            vif0_regs.stat |= (1u << 11);

        if (opcode == VIF_NOP)
        {
            continue;
        }
        else if (opcode == VIF_STCYCL)
        {
            vif0_regs.cycle = imm;
            continue;
        }
        else if (opcode == VIF_ITOP)
        {
            vif0_regs.itops = imm & 0x3FFu;
            continue;
        }
        else if (opcode == VIF_STMOD)
        {
            vif0_regs.mode = imm & 3u;
            continue;
        }
        else if (opcode == VIF_MARK)
        {
            vif0_regs.mark = imm;
            vif0_regs.stat |= (1u << 6);
            continue;
        }
        else if (opcode == VIF_FLUSHE || opcode == VIF_FLUSH || opcode == VIF_FLUSHA)
        {
            continue;
        }
        else if (opcode == VIF_STMASK)
        {
            // STMASK transfers one qword; only its first word is the mask
            // (PCSX2: vifCode_STMask sets tag.size = 1, then mask = data[0]).
            if (pos + 16u > sizeBytes)
                break;
            std::memcpy(&vif0_regs.mask, data + pos, sizeof(vif0_regs.mask));
            pos += 16u;
            continue;
        }
        else if (opcode == VIF_STROW)
        {
            if (pos + 16u > sizeBytes)
                break;
            std::memcpy(vif0_regs.row, data + pos, 16u);
            pos += 16u;
            continue;
        }
        else if (opcode == VIF_STCOL)
        {
            if (pos + 16u > sizeBytes)
                break;
            std::memcpy(vif0_regs.col, data + pos, 16u);
            pos += 16u;
            continue;
        }
        else if (opcode == VIF_MPG)
        {
            const uint32_t destAddr =
                (static_cast<uint32_t>(imm) * 8u) & (PS2_VU0_CODE_SIZE - 1u);
            const uint32_t instructionCount = (num == 0u) ? 256u : static_cast<uint32_t>(num);
            const uint32_t mpgBytes = instructionCount * 8u;
            if (sizeBytes - pos < mpgBytes)
                break;

            if (m_vu0Code)
            {
                uploadMicroMemory(m_vu0Code, PS2_VU0_CODE_SIZE, destAddr, data + pos, mpgBytes);
                markVU0CodeModified();
            }
            pos += mpgBytes;
            continue;
        }
        else if ((opcode & 0x60u) == 0x60u)
        {
            const uint8_t vn = static_cast<uint8_t>((opcode >> 2) & 0x3u);
            const uint8_t vl = static_cast<uint8_t>(opcode & 0x3u);
            const int components = static_cast<int>(vn) + 1;
            int bitsPerComponent = 32;
            switch (vl)
            {
            case 0:
                bitsPerComponent = 32;
                break;
            case 1:
                bitsPerComponent = 16;
                break;
            case 2:
                bitsPerComponent = 8;
                break;
            case 3:
                bitsPerComponent = (vn == 3u) ? 4 : 16;
                break;
            default:
                break;
            }
            const int bitsPerVector = (vl == 3u && vn == 3u) ? 16 : (components * bitsPerComponent);
            uint32_t bytesPerVector = static_cast<uint32_t>((bitsPerVector + 7) / 8);
            const uint32_t writeVectorCount = (num == 0u) ? 256u : static_cast<uint32_t>(num);
            uint32_t cl = vif0_regs.cycle & 0xFFu;
            uint32_t wl = (vif0_regs.cycle >> 8) & 0xFFu;
            if (cl == 0u)
                cl = 1u;
            if (wl == 0u)
                wl = 1u;
            uint32_t sourceVectorCount = writeVectorCount;
            if (cl < wl)
            {
                const uint32_t fullBlocks = writeVectorCount / wl;
                uint32_t remainder = writeVectorCount % wl;
                if (remainder > cl)
                    remainder = cl;
                sourceVectorCount = fullBlocks * cl + remainder;
            }
            uint32_t totalBytes = sourceVectorCount * bytesPerVector;
            totalBytes = (totalBytes + 3u) & ~3u;

            if (m_vu0Data && pos + totalBytes <= sizeBytes && vl == 0u)
            {
                uint32_t vuAddr = static_cast<uint32_t>(imm & 0x3FFu);
                if ((imm & 0x8000u) != 0u)
                    vuAddr = (vuAddr + (vif0_regs.tops & 0x3FFu)) & 0x3FFu;
                const uint8_t *srcBase = data + pos;
                uint32_t srcIndex = 0u;
                for (uint32_t writeIndex = 0; writeIndex < writeVectorCount; ++writeIndex)
                {
                    const uint32_t cyclePos = writeIndex % wl;
                    const bool sourceAvailable = (cl >= wl) || (cyclePos < cl);
                    uint32_t destVec = (cl >= wl) ? ((vuAddr + (writeIndex / wl) * cl + cyclePos) & 0x3FFu)
                                                  : ((vuAddr + writeIndex) & 0x3FFu);
                    const uint32_t destOff = destVec * 16u;
                    if (destOff + 16u > PS2_VU0_DATA_SIZE)
                    {
                        if (sourceAvailable && srcIndex < sourceVectorCount)
                            ++srcIndex;
                        continue;
                    }
                    if (!sourceAvailable || srcIndex >= sourceVectorCount)
                        continue;
                    const uint8_t *srcVec = srcBase + srcIndex * bytesPerVector;
                    ++srcIndex;
                    uint32_t lanes[4] = {0u, 0u, 0u, 0u};
                    std::memcpy(lanes, m_vu0Data + destOff, sizeof(lanes));
                    const uint32_t limit = (components > 4) ? 4u : static_cast<uint32_t>(components);
                    for (uint32_t c = 0; c < limit; ++c)
                    {
                        uint32_t scalar = 0u;
                        std::memcpy(&scalar, srcVec + c * 4u, sizeof(scalar));
                        lanes[c] = scalar;
                    }
                    _mm_storeu_si128(reinterpret_cast<__m128i *>(m_vu0Data + destOff), _mm_loadu_si128(reinterpret_cast<const __m128i *>(lanes)));
                }
            }
            pos += totalBytes;
            if (pos > sizeBytes)
                break;
            continue;
        }
        else
        {
            break;
        }
    }
}

void PS2Memory::processVIF1Data(uint32_t srcPhys, uint32_t sizeBytes)
{
    if (sizeBytes == 0u || srcPhys >= PS2_RAM_SIZE)
        return;

    const uint64_t requestedEnd = static_cast<uint64_t>(srcPhys) + static_cast<uint64_t>(sizeBytes);
    if (requestedEnd > static_cast<uint64_t>(PS2_RAM_SIZE))
        sizeBytes = PS2_RAM_SIZE - srcPhys;

    processVIF1Data(m_rdram + srcPhys, sizeBytes);
}

void PS2Memory::processVIF1Data(const uint8_t *inData, uint32_t inSizeBytes)
{
    if (inSizeBytes == 0u)
        return;

    // VIF is a byte stream: a single VIFcode (notably UNPACK) can straddle DMA
    // tags. Keep the unconsumed tail and resume parsing there so split payloads
    // are not silently dropped.
    if (m_vif1StreamPos != 0u)
    {
        if (m_vif1StreamPos >= m_vif1Stream.size())
        {
            m_vif1Stream.clear();
            m_vif1StreamPos = 0u;
        }
        else if (m_vif1StreamPos >= 0x10000u)
        {
            m_vif1Stream.erase(m_vif1Stream.begin(),
                               m_vif1Stream.begin() + static_cast<std::ptrdiff_t>(m_vif1StreamPos));
            m_vif1StreamPos = 0u;
        }
    }
    m_vif1Stream.insert(m_vif1Stream.end(), inData, inData + inSizeBytes);

    const uint8_t *const data = m_vif1Stream.data();
    const uint32_t sizeBytes = static_cast<uint32_t>(m_vif1Stream.size());

    uint32_t pos = m_vif1StreamPos;

    const auto submitImageContinuation = [&](uint32_t availableQw, bool directHl)
    {
        const uint32_t chunkQw = std::min(availableQw, m_vif1PendingPath2ImageQwc);
        const uint32_t chunkBytes = chunkQw * 16u;
        const bool last = m_vif1PendingPath2ImageQwc == chunkQw;
        std::vector<uint8_t> imagePacket(16u + chunkBytes, 0u);
        const uint64_t imageTag = static_cast<uint64_t>(chunkQw) |
                                  (last ? (1ull << 15) : 0ull) |
                                  (static_cast<uint64_t>(kGifFmtImage) << 58);
        std::memcpy(imagePacket.data(), &imageTag, sizeof(imageTag));
        std::memcpy(imagePacket.data() + 16u, data + pos, chunkBytes);
        submitGifPacket(GifPathId::Path2, imagePacket.data(),
                        static_cast<uint32_t>(imagePacket.size()), true, directHl);
        m_vif1PendingPath2ImageQwc -= chunkQw;
        m_vif1PendingPath2DirectHl = !last && directHl;
        pos += chunkBytes;
        return chunkQw;
    };

    while (pos + 4 <= sizeBytes)
    {
        if (m_vif1PendingPath2ImageQwc != 0u)
        {
            const uint32_t remaining = sizeBytes - pos;
            const uint32_t availableQw = remaining / 16u;
            if (availableQw != 0u && !hasDirectImageCommand(data + pos, remaining))
            {
                submitImageContinuation(availableQw, m_vif1PendingPath2DirectHl);
                continue;
            }
        }

        const uint32_t cmdStart = pos;
        uint32_t cmd;
        memcpy(&cmd, data + pos, 4);
        pos += 4;

        uint8_t opcode = (cmd >> 24) & 0x7F;
        uint16_t imm = cmd & 0xFFFF;
        uint8_t num = (cmd >> 16) & 0xFF;
        const bool irq = (cmd & 0x80000000u) != 0u;

        // Track most-recent command for VIFn_CODE emulation.
        vif1_regs.code = cmd;
        vif1_regs.num = num;
        if (irq)
            vif1_regs.stat |= (1u << 11); // INT

        if (opcode == VIF_NOP)
        {
            continue;
        }
        else if (opcode == VIF_STCYCL)
        {
            vif1_regs.cycle = imm;
            continue;
        }
        else if (opcode == VIF_OFFSET)
        {
            // VIF double-buffer setup. OFFSET clears DBF and resets TOPS to BASE.
            // Do not rewrite BASE from the previous TOPS value.
            vif1_regs.ofst = imm & 0x3FFu;
            vif1_regs.tops = vif1_regs.base & 0x3FFu;
            vif1_regs.stat &= ~(1u << 7); // clear DBF
            continue;
        }
        else if (opcode == VIF_BASE)
        {
            // BASE only updates the base register. TOPS changes on OFFSET/MSCAL.
            vif1_regs.base = imm & 0x3FFu;
            continue;
        }
        else if (opcode == VIF_ITOP)
        {
            // ITOP VIFcode writes pending ITOPS; VU XITOP observes it after MSCAL/MSCNT.
            vif1_regs.itops = imm & 0x3FFu;
            static const bool itopTrace = std::getenv("PES2014_VU_PATH") != nullptr;
            static uint32_t itopLogs = 0u;
            if (itopTrace && itopLogs < 200u)
            {
                ++itopLogs;
                std::fprintf(stderr, "[PES VIF itop] imm=%x itops=%x (log %u)\n",
                             imm, vif1_regs.itops, itopLogs);
            }
            continue;
        }
        else if (opcode == VIF_STMOD)
        {
            vif1_regs.mode = imm & 3u;
            continue;
        }
        else if (opcode == VIF_MSKPATH3)
        {
            // VIF command docs: MSKPATH3 uses IMMEDIATE bit 15.
            const bool wasMasked = m_path3Masked;
            m_path3Masked = (imm & 0x8000u) != 0u;
            if (wasMasked && !m_path3Masked)
                flushMaskedPath3Packets();
            continue;
        }
        else if (opcode == VIF_MARK)
        {
            vif1_regs.mark = imm;
            vif1_regs.stat |= (1u << 6); // MRK
            continue;
        }
        else if (opcode == VIF_FLUSHE || opcode == VIF_FLUSH || opcode == VIF_FLUSHA)
        {
            continue;
        }
        else if (opcode == VIF_MSCAL || opcode == VIF_MSCALF)
        {
            uint32_t startPC = (uint32_t)imm * 8u;

            const uint32_t runTop = vif1_regs.tops & 0x3FFu;
            const uint32_t runItop = vif1_regs.itops & 0x3FFu;
            vif1_regs.top = runTop;
            vif1_regs.itop = runItop;

            const bool dbf = (vif1_regs.stat & (1u << 7)) != 0u;
            if (dbf)
                vif1_regs.tops = vif1_regs.base & 0x3FFu;
            else
                vif1_regs.tops = (vif1_regs.base + vif1_regs.ofst) & 0x3FFu;
            vif1_regs.stat ^= (1u << 7); // toggle DBF

            if (m_vu1MscalCallback)
                m_vu1MscalCallback(startPC, runTop, runItop);
            static const bool mscalTrace = std::getenv("PES2014_VU_PATH") != nullptr;
            static uint32_t mscalLogs = 0u;
            if (mscalTrace && mscalLogs < 200u)
            {
                ++mscalLogs;
                std::fprintf(stderr, "[PES VIF mscal] pc=%x top=%x itop=%x nexttops=%x (log %u)\n",
                             startPC, runTop, runItop, vif1_regs.tops, mscalLogs);
            }
            continue;
        }
        else if (opcode == VIF_MSCNT)
        {
            const uint32_t runTop = vif1_regs.tops & 0x3FFu;
            const uint32_t runItop = vif1_regs.itops & 0x3FFu;
            vif1_regs.top = runTop;
            vif1_regs.itop = runItop;

            const bool dbf = (vif1_regs.stat & (1u << 7)) != 0u;
            if (dbf)
                vif1_regs.tops = vif1_regs.base & 0x3FFu;
            else
                vif1_regs.tops = (vif1_regs.base + vif1_regs.ofst) & 0x3FFu;
            vif1_regs.stat ^= (1u << 7); // toggle DBF

            if (m_vu1MscntCallback)
                m_vu1MscntCallback(runTop, runItop);
            static const bool mscntLogEnable = std::getenv("PES2014_VU_PATH") != nullptr;
            static uint32_t mscntLogs = 0u;
            if (mscntLogEnable && mscntLogs < 200u)
            {
                ++mscntLogs;
                std::fprintf(stderr, "[PES VIF mscnt] top=%x itop=%x nexttops=%x (log %u)\n",
                             runTop, runItop, vif1_regs.tops, mscntLogs);
            }
            continue;
        }
        else if (opcode == VIF_STMASK)
        {
            // STMASK transfers one qword; only its first word is the mask
            // (PCSX2: vifCode_STMask sets tag.size = 1, then mask = data[0]).
            if (pos + 16 > sizeBytes)
            {
                pos = cmdStart;
                break;
            }
            uint32_t maskValue = 0;
            std::memcpy(&maskValue, data + pos, sizeof(maskValue));
            vif1_regs.mask = maskValue;
            pos += 16;
            continue;
        }
        else if (opcode == VIF_STROW)
        {
            if (pos + 16 > sizeBytes)
            {
                pos = cmdStart;
                break;
            }
            std::memcpy(vif1_regs.row, data + pos, 16);
            pos += 16;
            continue;
        }
        else if (opcode == VIF_STCOL)
        {
            if (pos + 16 > sizeBytes)
            {
                pos = cmdStart;
                break;
            }
            std::memcpy(vif1_regs.col, data + pos, 16);
            pos += 16;
            continue;
        }
        else if (opcode == VIF_MPG)
        {
            const uint32_t destAddr =
                (static_cast<uint32_t>(imm) * 8u) & (PS2_VU1_CODE_SIZE - 1u);
            const uint32_t instructionCount = (num == 0u) ? 256u : static_cast<uint32_t>(num);
            const uint32_t mpgBytes = instructionCount * 8u;
            if (sizeBytes - pos < mpgBytes)
            {
                pos = cmdStart;
                break;
            }

            if (m_vu1Code)
            {
                uploadMicroMemory(m_vu1Code, PS2_VU1_CODE_SIZE, destAddr, data + pos, mpgBytes);
                markVU1CodeModified();
            }
            pos += mpgBytes;
            continue;
        }
        else if (opcode == VIF_DIRECT || opcode == VIF_DIRECTHL)
        {
            uint32_t qwCount = imm;
            if (qwCount == 0)
                qwCount = 65536;
            const uint32_t availableQw = (sizeBytes - pos) / 16u;
            if (qwCount > availableQw)
            {
                // Wait for the rest of this DIRECT block instead of consuming a
                // partial payload (that would desync the GIF stream).
                pos = cmdStart;
                break;
            }

            const bool directHl = opcode == VIF_DIRECTHL;
            if (qwCount != 0u && m_vif1PendingPath2ImageQwc != 0u)
            {
                // A GIF IMAGE can span multiple VIF DIRECT commands. Only the
                // data following each VIF command belongs in the GIF payload.
                qwCount -= submitImageContinuation(qwCount, directHl);
            }

            if (qwCount > 0)
            {
                submitGifPacket(GifPathId::Path2, data + pos, qwCount * 16, true, directHl);

                const uint32_t pendingImageQw = pendingGifImageQwc(data + pos, qwCount * 16u);
                if (pendingImageQw != 0u)
                {
                    m_vif1PendingPath2ImageQwc = pendingImageQw;
                    m_vif1PendingPath2DirectHl = directHl;
                }
            }

            pos += qwCount * 16;
            continue;
        }
        else if ((opcode & 0x60) == 0x60)
        {
            uint8_t vn = (opcode >> 2) & 0x3;
            uint8_t vl = opcode & 0x3;
            const bool maskEnable = (opcode & 0x10u) != 0u;
            int components = vn + 1;
            int bitsPerComponent = 32;
            switch (vl)
            {
            case 0:
                bitsPerComponent = 32;
                break;
            case 1:
                bitsPerComponent = 16;
                break;
            case 2:
                bitsPerComponent = 8;
                break;
            case 3:
                bitsPerComponent = (vn == 3) ? 4 : 16;
                break;
            default:
                break;
            }
            int bitsPerVector = (vl == 3 && vn == 3) ? 16 : (components * bitsPerComponent);
            uint32_t bytesPerVector = (bitsPerVector + 7) / 8;
            // UNPACK semantics: NUM is 8-bit and NUM==0 means 256 vectors (writes).
            const uint32_t writeVectorCount = (num == 0u) ? 256u : static_cast<uint32_t>(num);

            // STCYCL controls write cycles for UNPACK.
            uint32_t cl = vif1_regs.cycle & 0xFFu;
            uint32_t wl = (vif1_regs.cycle >> 8) & 0xFFu;
            if (cl == 0u)
                cl = 1u;
            if (wl == 0u)
                wl = 1u;

            uint32_t sourceVectorCount = writeVectorCount;
            if (cl < wl)
            {
                const uint32_t fullBlocks = writeVectorCount / wl;
                uint32_t remainder = writeVectorCount % wl;
                if (remainder > cl)
                    remainder = cl;
                sourceVectorCount = fullBlocks * cl + remainder;
            }

            uint32_t totalBytes = sourceVectorCount * bytesPerVector;
            totalBytes = (totalBytes + 3) & ~3u;

            uint32_t vuAddr = (uint32_t)imm & 0x3FFu;
            if ((imm & 0x8000u) != 0u)
                vuAddr = (vuAddr + (vif1_regs.tops & 0x3FFu)) & 0x3FFu;

            const bool zeroExtend = (imm & 0x4000u) != 0u;

            // Vertex-input triage: log 16-byte (V4-32) unpacks, which carry the
            // per-vertex arrays the VU1 program loads.
            static const bool vtxTrace = std::getenv("PES2014_VU_INPUT") != nullptr;
            static uint32_t vtxLogs = 0u;
            if (vtxTrace && vtxLogs < 400u && bytesPerVector == 16u &&
                vuAddr >= 0x200u && vuAddr < 0x300u)
            {
                ++vtxLogs;
                uint64_t first = 0u, second = 0u;
                if (pos + 8u <= sizeBytes)
                    std::memcpy(&first, data + pos, sizeof(first));
                if (pos + 16u <= sizeBytes)
                    std::memcpy(&second, data + pos + 8u, sizeof(second));
                std::fprintf(stderr,
                             "[PES VIF vtx] op=%02x imm=%04x num=%u cl=%u wl=%u tops=%x vuAddr=%x"
                             " vectors=%u bytes=%u fits=%d src=%016llx/%016llx\n",
                             opcode, imm, num, cl, wl, vif1_regs.tops & 0x3FFu, vuAddr,
                             writeVectorCount, totalBytes,
                             (pos + totalBytes <= sizeBytes) ? 1 : 0,
                             static_cast<unsigned long long>(first),
                             static_cast<unsigned long long>(second));
            }

            // Vertex-record triage: log every UNPACK that lands in the low VU
            // data area (0x00..0x40) where the per-vertex records live.
            {
                static const bool lowTrace = std::getenv("PES2014_VU_LOWTRACE") != nullptr;
                static uint32_t lowLogs = 0u;
                const bool hitsLow = ((vuAddr + writeVectorCount) > 0x18u && vuAddr < 0x28u) ||
                                     ((vuAddr + writeVectorCount) > 0x1F0u && vuAddr < 0x200u) ||
                                     ((vuAddr + writeVectorCount) > 0x3F0u && vuAddr < 0x400u);
                if (lowTrace && hitsLow && lowLogs < 300u)
                {
                    ++lowLogs;
                    std::fprintf(stderr,
                                 "[PES VIF low] op=%02x imm=%04x num=%u vl=%u vn=%u cl=%u wl=%u"
                                 " tops=%x vuAddr=%x bpv=%u vectors=%u bytes=%u fits=%d\n",
                                 opcode, imm, num, static_cast<unsigned>(vl), static_cast<unsigned>(vn),
                                 cl, wl, vif1_regs.tops & 0x3FFu, vuAddr, bytesPerVector,
                                 writeVectorCount, totalBytes,
                                 (pos + totalBytes <= sizeBytes) ? 1 : 0);
                    for (uint32_t q = 0; q < 6u && pos + (q + 1u) * 16u <= sizeBytes; ++q)
                    {
                        uint64_t a = 0u, b = 0u;
                        std::memcpy(&a, data + pos + q * 16u, 8u);
                        std::memcpy(&b, data + pos + q * 16u + 8u, 8u);
                        std::fprintf(stderr, "    src[%u]=%016llx/%016llx\n", q,
                                     static_cast<unsigned long long>(a),
                                     static_cast<unsigned long long>(b));
                    }
                }
            }

            // PES triage: the VU1 input buffers hold GIF tag templates that the
            // EE unpacks (0x1E0/0x3E0 bases). Log every UNPACK that touches
            // them so a missing/truncated write is visible.
            static const bool unpackTrace = std::getenv("PES2014_VU_PATH") != nullptr;
            if (unpackTrace)
            {
                bool touches = false;
                for (uint32_t i = 0; i < writeVectorCount && !touches; ++i)
                {
                    const uint32_t q = (vuAddr + i) & 0x3FFu;
                    if (q == 0x1E0u || q == 0x3E0u)
                        touches = true;
                }
                static uint32_t unpackLogs = 0u;
                if (touches && unpackLogs < 240u)
                {
                    ++unpackLogs;
                    uint64_t src0 = 0u, src1 = 0u;
                    if (pos + 16u <= sizeBytes)
                    {
                        std::memcpy(&src0, data + pos, sizeof(src0));
                        std::memcpy(&src1, data + pos + 8u, sizeof(src1));
                    }
                    std::fprintf(stderr,
                                 "[PES VIF unpack] imm=%04x num=%u vl=%u vn=%u cl=%u wl=%u tops=%x"
                                 " vuAddr=%x vectors=%u srcVectors=%u bytes=%u fits=%d"
                                 " src=%016llx/%016llx\n",
                                 imm, num, vl, vn, cl, wl, vif1_regs.tops & 0x3FFu,
                                 vuAddr, writeVectorCount, sourceVectorCount, totalBytes,
                                 (pos + totalBytes <= sizeBytes) ? 1 : 0,
                                 static_cast<unsigned long long>(src0),
                                 static_cast<unsigned long long>(src1));
                }
            }

            if (m_vu1Data && totalBytes > 0 && pos + totalBytes > sizeBytes)
            {
                static const bool truncTrace = std::getenv("PES2014_VIF_TRUNC") != nullptr;
                static unsigned truncLogs = 0u;
                static unsigned truncCount = 0u;
                ++truncCount;
                if (truncTrace && truncLogs < 48u)
                {
                    ++truncLogs;
                    std::fprintf(stderr,
                                 "[PES VIF trunc] #%u op=%02x imm=%04x num=%u vl=%u vn=%u cl=%u wl=%u"
                                 " tops=%x vuAddr=%x bpv=%u vectors=%u bytes=%u avail=%u\n",
                                 truncCount, opcode, imm, num, static_cast<unsigned>(vl),
                                 static_cast<unsigned>(vn), cl, wl, vif1_regs.tops & 0x3FFu,
                                 vuAddr, bytesPerVector, writeVectorCount, totalBytes, sizeBytes - pos);
                }
            }

            if (m_vu1Data && totalBytes > 0 && pos + totalBytes > sizeBytes)
            {
                // Not all vectors arrived yet. Rewind to the UNPACK command so
                // the next DMA tag completes it instead of dropping the data.
                pos = cmdStart;
                break;
            }

            if (m_vu1Data && totalBytes > 0 && pos + totalBytes <= sizeBytes)
            {
                const uint8_t *srcBase = data + pos;
                uint32_t srcIndex = 0u;
                for (uint32_t writeIndex = 0; writeIndex < writeVectorCount; ++writeIndex)
                {
                    const uint32_t cyclePos = writeIndex % wl;
                    const bool sourceAvailable = (cl >= wl) || (cyclePos < cl);

                    uint32_t destVec = 0;
                    if (cl >= wl)
                    {
                        destVec = (vuAddr + (writeIndex / wl) * cl + cyclePos) & 0x3FFu;
                    }
                    else
                    {
                        destVec = (vuAddr + writeIndex) & 0x3FFu;
                    }

                    uint32_t destOff = destVec * 16u;
                    if (destOff + 16u > PS2_VU1_DATA_SIZE)
                    {
                        if (sourceAvailable && srcIndex < sourceVectorCount)
                            ++srcIndex;
                        continue;
                    }

                    uint32_t lanes[4] = {0u, 0u, 0u, 0u};
                    std::memcpy(lanes, m_vu1Data + destOff, sizeof(lanes));
                    uint32_t decompressed[4] = {lanes[0], lanes[1], lanes[2], lanes[3]};
                    bool decoded = false;

                    const uint8_t *srcVec = nullptr;
                    if (sourceAvailable && srcIndex < sourceVectorCount)
                    {
                        srcVec = srcBase + srcIndex * bytesPerVector;
                        ++srcIndex;
                        decoded = true;
                    }

                    auto extend16 = [&](uint16_t raw) -> uint32_t
                    {
                        if (zeroExtend)
                            return static_cast<uint32_t>(raw);
                        return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(raw)));
                    };

                    auto extend8 = [&](uint8_t raw) -> uint32_t
                    {
                        if (zeroExtend)
                            return static_cast<uint32_t>(raw);
                        return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(raw)));
                    };

                    bool handledFormat = true;
                    if (!decoded)
                    {
                        handledFormat = false;
                    }
                    else if (vl == 0u)
                    {
                        if (components == 1)
                        {
                            uint32_t scalar = 0;
                            std::memcpy(&scalar, srcVec, sizeof(scalar));
                            decompressed[0] = scalar;
                            decompressed[1] = scalar;
                            decompressed[2] = scalar;
                            decompressed[3] = scalar;
                        }
                        else
                        {
                            const uint32_t limit = (components > 4) ? 4u : static_cast<uint32_t>(components);
                            for (uint32_t c = 0; c < limit; ++c)
                            {
                                uint32_t scalar = 0;
                                std::memcpy(&scalar, srcVec + c * 4u, sizeof(scalar));
                                decompressed[c] = scalar;
                            }
                        }
                    }
                    else if (vl == 1u)
                    {
                        if (components == 1)
                        {
                            uint16_t raw = 0;
                            std::memcpy(&raw, srcVec, sizeof(raw));
                            const uint32_t scalar = extend16(raw);
                            decompressed[0] = scalar;
                            decompressed[1] = scalar;
                            decompressed[2] = scalar;
                            decompressed[3] = scalar;
                        }
                        else
                        {
                            const uint32_t limit = (components > 4) ? 4u : static_cast<uint32_t>(components);
                            for (uint32_t c = 0; c < limit; ++c)
                            {
                                uint16_t raw = 0;
                                std::memcpy(&raw, srcVec + c * 2u, sizeof(raw));
                                decompressed[c] = extend16(raw);
                            }
                        }
                    }
                    else if (vl == 2u)
                    {
                        if (components == 1)
                        {
                            const uint32_t scalar = extend8(srcVec[0]);
                            decompressed[0] = scalar;
                            decompressed[1] = scalar;
                            decompressed[2] = scalar;
                            decompressed[3] = scalar;
                        }
                        else
                        {
                            const uint32_t limit = (components > 4) ? 4u : static_cast<uint32_t>(components);
                            for (uint32_t c = 0; c < limit; ++c)
                            {
                                decompressed[c] = extend8(srcVec[c]);
                            }
                        }
                    }
                    else if (vl == 3u && vn == 3u)
                    {
                        // V4-5: packed color-like format in a single 16-bit value.
                        uint16_t packed = 0;
                        std::memcpy(&packed, srcVec, sizeof(packed));
                        decompressed[0] = (packed & 0x1fu) << 3u;
                        decompressed[1] = ((packed >> 5u) & 0x1fu) << 3u;
                        decompressed[2] = ((packed >> 10u) & 0x1fu) << 3u;
                        decompressed[3] = (packed >> 15u) << 7u;
                    }
                    else
                    {
                        handledFormat = false;
                    }

                    if (handledFormat && components == 2)
                    {
                        decompressed[2] = decompressed[0];
                        decompressed[3] = decompressed[1];
                    }
                    if (handledFormat && components == 3)
                    {
                        const uint8_t *next = srcVec + bytesPerVector;
                        const size_t elementBytes = static_cast<size_t>(bitsPerComponent / 8);
                        decompressed[3] = 0u;
                        if (static_cast<size_t>(data + sizeBytes - next) >= elementBytes)
                        {
                            uint32_t raw = 0u;
                            std::memcpy(&raw, next, elementBytes);
                            decompressed[3] = elementBytes == 4u ? raw :
                                elementBytes == 2u ? extend16(static_cast<uint16_t>(raw)) :
                                extend8(static_cast<uint8_t>(raw));
                        }
                    }

                    // Unknown compressed format fallback: preserve legacy raw-copy behavior.
                    if (!handledFormat && decoded && !maskEnable && (vif1_regs.mode == 0u || vif1_regs.mode == 3u))
                    {
                        uint32_t copyBytes = (bytesPerVector < 16u) ? bytesPerVector : 16u;
                        std::memcpy(m_vu1Data + destOff, srcVec, copyBytes);
                        continue;
                    }

                    const bool canAdd = (vl != 3u || vn != 3u);
                    const uint32_t mode = vif1_regs.mode & 3u;
                    const uint32_t colIdx = (cyclePos > 3u) ? 3u : cyclePos;
                    const uint32_t maskCycle = (cyclePos > 3u) ? 3u : cyclePos;

                    for (uint32_t field = 0u; field < 4u; ++field)
                    {
                        uint32_t maskSpec = 0u;
                        if (maskEnable)
                        {
                            const uint32_t shift = ((maskCycle * 4u) + field) * 2u;
                            maskSpec = (vif1_regs.mask >> shift) & 0x3u;
                        }

                        // In fill-write cycles with suspended source reads, treat raw-data selections as row-fill.
                        if (!decoded && maskSpec == 0u)
                            maskSpec = 1u;

                        uint32_t writeVal = lanes[field];
                        if (maskSpec == 0u)
                        {
                            if (handledFormat)
                            {
                                writeVal = decompressed[field];
                                if (canAdd && (mode == 1u || mode == 2u))
                                {
                                    writeVal = writeVal + vif1_regs.row[field];
                                    if (mode == 2u)
                                        vif1_regs.row[field] = writeVal;
                                }
                                if (canAdd && mode == 3u)
                                    vif1_regs.row[field] = writeVal;
                            }
                        }
                        else if (maskSpec == 1u)
                        {
                            writeVal = vif1_regs.row[field];
                        }
                        else if (maskSpec == 2u)
                        {
                            writeVal = vif1_regs.col[colIdx];
                        }
                        else
                        {
                            continue; // write-protect
                        }

                        lanes[field] = writeVal;
                    }

                    std::memcpy(m_vu1Data + destOff, lanes, sizeof(lanes));
                }
            }
            pos += totalBytes;

            if (pos > sizeBytes)
                break;
            continue;
        }
        else
        {
            continue;
        }
    }

    m_vif1StreamPos = pos;
    if (m_vif1StreamPos >= m_vif1Stream.size())
    {
        m_vif1Stream.clear();
        m_vif1StreamPos = 0u;
    }
}
