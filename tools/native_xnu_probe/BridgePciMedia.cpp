/* MIT. Genuine IOStorage -> IOMedia -> IOMediaBSDClient diagnostic.
 * The media remains read-only and accepts only the separate 2 MiB fixture.
 * This is not a filesystem driver or a production iOS root-disk profile.
 */
#include <IOKit/IOBSD.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/storage/IOMediaBSDClient.h>
#include <IOKit/storage/IOStorage.h>
#include <kern/debug.h>
#include <libkern/c++/OSSet.h>
#include <libkern/c++/OSString.h>
#include <pexpert/pexpert.h>
#include "BridgePciMediaDevice.h"
#include "pci_contract.generated.h"

namespace {
constexpr uint64_t diskBytes = 2097152;

void receipt(const char *phase, uint64_t a = 0, uint64_t b = 0) {
    kprintf("PLB_XNU_MEDIA nonce=" PLB_PCI_NONCE " phase=%s a=%llu b=%llu\n", phase, a, b);
}

uint8_t expectedByte(uint64_t offset) {
    return static_cast<uint8_t>(offset * 17 + (offset >> 9) * 29 + plb_pci_seed[offset % 16]);
}
} // namespace

class BridgePciMediaStorage : public IOStorage {
    OSDeclareDefaultStructors(BridgePciMediaStorage) IOLock *gate = nullptr;
    OSSet *readers = nullptr;
    uint8_t page[4096] = {};
    bool online = false;

  protected:
    bool handleOpen(IOService *client, IOOptionBits options, void *access) override {
        if (!online || !client || options ||
            reinterpret_cast<uintptr_t>(access) != kIOStorageAccessReader) {
            return false;
        }
        return readers->containsObject(client) || readers->setObject(client);
    }
    bool handleIsOpen(const IOService *client) const override {
        return readers && (client ? readers->containsObject(client) : readers->getCount() != 0);
    }
    void handleClose(IOService *client, IOOptionBits) override {
        if (readers && client) {
            readers->removeObject(client);
        }
    }

  public:
    bool init(OSDictionary *properties = nullptr) override {
        if (!IOStorage::init(properties)) {
            return false;
        }
        gate = IOLockAlloc();
        readers = OSSet::withCapacity(2);
        return gate && readers;
    }
    bool start(IOService *provider) override {
        online = IOStorage::start(provider) && plb_media_device_open();
        return online;
    }
    void free() override {
        if (readers) {
            readers->release();
        }
        if (gate) {
            IOLockFree(gate);
        }
        IOStorage::free();
    }
    bool finish() {
        IOLockLock(gate);
        online = false;
        bool reset = plb_media_device_close();
        IOLockUnlock(gate);
        return reset;
    }
    void read(IOService *client, UInt64 start, IOMemoryDescriptor *buffer, IOStorageAttributes *,
              IOStorageCompletion *completion) override {
        IOReturn status = kIOReturnSuccess;
        uint64_t done = 0;
        if (!buffer || !(buffer->getDirection() & kIODirectionIn) || !isOpen(client)) {
            complete(completion, kIOReturnBadArgument);
            return;
        }
        uint64_t count = buffer->getLength();
        if (start > diskBytes || count > diskBytes - start) {
            complete(completion, kIOReturnBadArgument);
            return;
        }
        IOLockLock(gate);
        if (!online) {
            status = kIOReturnNotReady;
        }
        while (status == kIOReturnSuccess && done < count) {
            uint64_t offset = start + done;
            unsigned within = static_cast<unsigned>(offset & 4095);
            uint64_t bytes = count - done;
            if (bytes > 4096 - within) {
                bytes = 4096 - within;
            }
            if (!plb_media_device_read(offset & ~4095ULL, page)) {
                online = false;
                status = kIOReturnIOError;
                break;
            }
            if (buffer->writeBytes(done, page + within, bytes) != bytes) {
                status = kIOReturnVMError;
                break;
            }
            done += bytes;
        }
        IOLockUnlock(gate);
        complete(completion, status, done);
    }
    void write(IOService *, UInt64, IOMemoryDescriptor *, IOStorageAttributes *,
               IOStorageCompletion *completion) override {
        complete(completion, kIOReturnNotWritable);
    }
    IOReturn unmap(IOService *, IOStorageExtent *, UInt32, IOStorageUnmapOptions) override {
        return kIOReturnNotWritable;
    }
    IOReturn synchronize(IOService *, UInt64, UInt64, IOStorageSynchronizeOptions) override {
        return kIOReturnUnsupported;
    }
};

OSDefineMetaClassAndStructors(BridgePciMediaStorage, IOStorage)

    static bool verifyMedia(IOMedia *media, IOService *client) {
    if (!media->open(client, 0, kIOStorageAccessReader)) {
        receipt("open_failed");
        return false;
    }
    IOBufferMemoryDescriptor *buffer = IOBufferMemoryDescriptor::withCapacity(4096, kIODirectionIn);
    if (!buffer) {
        media->close(client);
        return false;
    }
    bool pass = true;
    uint64_t digest = 14695981039346656037ULL;
    for (uint64_t offset = 0; pass && offset < diskBytes; offset += 4096) {
        UInt64 actual = 0;
        IOReturn status =
            static_cast<IOStorage *>(media)->read(client, offset, buffer, nullptr, &actual);
        if (status != kIOReturnSuccess || actual != 4096) {
            receipt("media_read_failed", offset, status);
            pass = false;
            break;
        }
        const uint8_t *bytes = static_cast<const uint8_t *>(buffer->getBytesNoCopy());
        for (unsigned i = 0; i < 4096; i++) {
            if (bytes[i] != expectedByte(offset + i)) {
                receipt("mismatch", offset + i, bytes[i]);
                pass = false;
                break;
            }
            digest = (digest ^ bytes[i]) * 1099511628211ULL;
        }
    }
    if (pass) {
        receipt("media_read_pass", diskBytes, digest);
        // A 512-byte-aligned read crossing two DMA pages exercises bounce slicing.
        UInt64 actual = 0;
        pass = static_cast<IOStorage *>(media)->read(client, 512, buffer, nullptr, &actual) ==
                   kIOReturnSuccess &&
               actual == 4096;
        const uint8_t *bytes = static_cast<const uint8_t *>(buffer->getBytesNoCopy());
        for (unsigned i = 0; pass && i < 4096; i++) {
            pass = bytes[i] == expectedByte(512 + i);
        }
        if (pass) {
            receipt("cross_page_pass", 512, 4096);
        }
    }
    buffer->release();
    IOBufferMemoryDescriptor *out = IOBufferMemoryDescriptor::withCapacity(512, kIODirectionOut);
    uint64_t requests = plb_media_device_requests();
    if (pass && out) {
        UInt64 actual = 0;
        // The public iOS IOMedia returns LockedWrite for read-only media;
        // the transport itself returns NotWritable if called directly.
        pass = static_cast<IOStorage *>(media)->write(client, 0, out, nullptr, &actual) ==
                   kIOReturnLockedWrite &&
               actual == 0 && requests == plb_media_device_requests();
        if (pass) {
            receipt("write_refused", requests, plb_media_device_requests());
        }
    } else {
        pass = false;
    }
    if (out) {
        out->release();
    }
    media->close(client);
    return pass;
}

extern "C" void plb_bridge_pci_media_probe(void);

extern "C" void plb_bridge_pci_media_probe(void) {
    unsigned enabled = 0;
    if (!PE_parse_boot_argn("bridge-pci-probe", &enabled, sizeof(enabled)) || enabled != 1) {
        return;
    }
    receipt("entry");
    IOService *root = IOService::getServiceRoot();
    BridgePciMediaStorage *storage = new BridgePciMediaStorage;
    IOMedia *media = nullptr;
    IOMediaBSDClient *bsd = nullptr;
    bool initialized = root && storage && storage->init();
    bool pass = initialized && storage->attach(root) && storage->start(root);
    if (pass) {
        media = new IOMedia;
        pass =
            media && media->init(0, diskBytes, 512, 0, true, false, "") && media->attach(storage);
        if (pass) {
            media->setName("Bridge synthetic PCI media");
            media->registerService();
            receipt("media_published", media->getSize(), media->isWritable());
            bsd = new IOMediaBSDClient;
            pass = bsd && bsd->init() && bsd->attach(media) && bsd->start(media);
            OSString *name = OSDynamicCast(OSString, media->getProperty(kIOBSDNameKey));
            pass = pass && name && name->getLength() > 0;
            if (pass) {
                receipt("bsd_node", 1, 0);
                pass = verifyMedia(media, root);
            }
        }
    }
    bool reset = initialized ? storage->finish() : plb_media_device_close();
    if (reset) {
        receipt("device_reset");
    }
    receipt(pass && reset ? "pass" : "failed");
    receipt("guest_shutdown");
    // The retained services belong solely to this diagnostic guest. Power off
    // after all synchronous reads and device reset; never restart Android.
    register uint64_t command __asm__("x0") = 0x84000008;
    __asm__ volatile("hvc #0" : "+r"(command) : : "memory");
    for (;;) {
        __asm__ volatile("wfe");
    }
}
