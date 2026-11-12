#include "runtime/gs/ps2_gif_arbiter.h"
#include <cstring>
#include <utility>

GifArbiter::GifArbiter(ProcessPacketFn processFn)
    : m_processFn(std::move(processFn))
{
}

bool GifArbiter::isImagePacket(const uint8_t *data, uint32_t sizeBytes)
{
    if (!data || sizeBytes < 16u)
        return false;

    uint64_t tagLo = 0;
    std::memcpy(&tagLo, data, sizeof(tagLo));
    const uint8_t flg = static_cast<uint8_t>((tagLo >> 58) & 0x3u);
    // FLG=3 ("disabled" in the GS manual) behaves exactly like IMAGE: NLOOP qwords of image data.
    return flg == 2u || flg == 3u;
}

void GifArbiter::submit(GifPathId pathId, const uint8_t *data, uint32_t sizeBytes, bool path2DirectHl)
{
    if (!data || sizeBytes < 16 || !m_processFn)
        return;

    GifArbiterPacket pkt;
    pkt.pathId = pathId;
    pkt.path2DirectHl = (pathId == GifPathId::Path2) && path2DirectHl;
    pkt.path3Image = (pathId == GifPathId::Path3) && isImagePacket(data, sizeBytes);
    pkt.data.resize(sizeBytes);
    std::memcpy(pkt.data.data(), data, sizeBytes);
    m_queue.push_back(std::move(pkt));
}

void GifArbiter::drain()
{
    if (!m_processFn)
        return;

    // Triage: classify clear-like packets (untextured sprite) by GIF path and
    // position in the queue, to see whether the frame-ending black fill comes
    // from PATH1 (VU1) or PATH3 (EE DMA).
    static const bool pathTrace = std::getenv("PES2014_GIF_PATH") != nullptr;
    static uint32_t pathLogs = 0u;
    if (pathTrace)
    {
        for (size_t i = 0; i < m_queue.size(); ++i)
        {
            const GifArbiterPacket &p = m_queue[i];
            if (p.data.size() < 16u)
                continue;
            uint64_t tagLo = 0u;
            std::memcpy(&tagLo, p.data.data(), sizeof(tagLo));
            const uint32_t nloop = static_cast<uint32_t>(tagLo & 0x7FFFu);
            const uint32_t eop = static_cast<uint32_t>((tagLo >> 15) & 1u);
            const uint32_t flg = static_cast<uint32_t>((tagLo >> 58) & 3u);
            const uint32_t nreg = static_cast<uint32_t>((tagLo >> 60) & 0xFu);
            const uint32_t prim = static_cast<uint32_t>((tagLo >> 47) & 0x7FFu);
            const uint32_t pre = static_cast<uint32_t>((tagLo >> 46) & 1u);
            const uint32_t type = prim & 7u;
            const uint32_t tme = (prim >> 4) & 1u;
            if (pathLogs < 600u && flg == 0u && type == 6u && tme == 0u && nloop != 0u)
            {
                ++pathLogs;
                std::fprintf(stderr,
                             "[PES GIF clear] path=%d idx=%u/%u size=%u nloop=%u nreg=%u pre=%u prim=%03x\n",
                             static_cast<int>(p.pathId), static_cast<unsigned>(i),
                             static_cast<unsigned>(m_queue.size()), static_cast<unsigned>(p.data.size()),
                             nloop, nreg, pre, prim);
            }
        }
    }

    // The GS consumes each path at the moment our emulated DMA / XGKICK hands
    // the packet over, so submission order is the faithful order.  The old
    // implementation sorted by path, which moved every PATH1 (VU1) packet
    // ahead of the PATH3 (EE) ones: a framebuffer clear submitted before the
    // frame's geometry then ran after it and wiped the picture.  Its
    // comparator also mixed path3Image / path2DirectHl into a relation that is
    // not a strict weak ordering, so std::stable_sort's behaviour was
    // undefined.  Packets are already atomic, so keep them in FIFO order.
    for (size_t i = 0; i < m_queue.size(); ++i)
    {
        auto &pkt = m_queue[i];
        if (!pkt.data.empty())
            m_processFn(pkt.data.data(), static_cast<uint32_t>(pkt.data.size()));
    }
    m_queue.clear();
}
