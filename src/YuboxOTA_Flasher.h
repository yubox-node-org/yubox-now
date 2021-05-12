#ifndef _YUBOX_OTA_FLASHER_H_
#define _YUBOX_OTA_FLASHER_H_

#include <Arduino.h>
#include <functional>

//#define DEBUG_YUBOX_OTA

typedef enum
{
  YBX_OTA_IDLE,           // Código ocioso, o el archivo está siendo ignorado
  YBX_OTA_SPIFFS_WRITE,   // Se está escribiendo el archivo a SPIFFS
  YBX_OTA_FIRMWARE_FLASH  // Se está escribiendo a flash de firmware
} YuboxOTA_operationWithFile;

typedef std::function<void (const char *, bool, unsigned long) > YuboxOTA_Flasher_FileStart_func_cb;
typedef std::function<void (const char *, bool, unsigned long, unsigned long) > YuboxOTA_Flasher_FileProgress_func_cb;
typedef std::function<void (const char *, bool, unsigned long) > YuboxOTA_Flasher_FileEnd_func_cb;

class YuboxOTA_Flasher
{
protected:

    // Function objects to invoke for progress callbacks
    YuboxOTA_Flasher_FileStart_func_cb _filestart_cb;
    YuboxOTA_Flasher_FileProgress_func_cb _fileprogress_cb;
    YuboxOTA_Flasher_FileEnd_func_cb _fileend_cb;

public:
    YuboxOTA_Flasher(void) = default;
    void setProgressCallbacks(
        YuboxOTA_Flasher_FileStart_func_cb filestart_cb,
        YuboxOTA_Flasher_FileProgress_func_cb fileprogress_cb,
        YuboxOTA_Flasher_FileEnd_func_cb fileend_cb
    ) {
        _filestart_cb = filestart_cb;
        _fileprogress_cb = fileprogress_cb;
        _fileend_cb = fileend_cb;
    }
    virtual ~YuboxOTA_Flasher() = default;

    // Called in order to setup everything for receiving update chunks
    virtual bool startUpdate(void) = 0;

    // Called when stream EOF has been encountered but tar data has not finished (truncated file)
    virtual void truncateUpdate(void) = 0;

    // Called when tar data stream has finished properly
    virtual bool finishUpdate(void) = 0;

    // Check if current update process cannot continue due to an unrecoverable error
    virtual bool isUpdateRejected(void) = 0;

    // Get last error message from update process, or empty string on success
    virtual String getLastErrorMessage(void) = 0;

    // Start new file entry from uncompressed update stream
    virtual bool startFile(const char *, unsigned long long) = 0;

    // Append data to file entry opened with startFile()
    virtual bool appendFileData(const char *, unsigned long long, unsigned char *, int) = 0;

    // Finish data on opened file entry
    virtual bool finishFile(const char *, unsigned long long) = 0;

    // Check if this firmware update needs a reboot to be applied
    virtual bool shouldReboot(void) = 0;

    virtual bool canRollBack(void) = 0;

    virtual bool doRollBack(void) = 0;
};

#endif