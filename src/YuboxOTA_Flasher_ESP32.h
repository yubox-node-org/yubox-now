#ifndef _YUBOX_OTA_FLASHER_ESP32_H_
#define _YUBOX_OTA_FLASHER_ESP32_H_

#include "YuboxOTA_Flasher.h"

#include "FS.h"
#include <vector>

class YuboxOTA_Flasher_ESP32 : public YuboxOTA_Flasher
{
private:
    bool _uploadRejected;
    String _responseMsg;

    YuboxOTA_operationWithFile _tgzupload_currentOp;
    bool _tgzupload_foundFirmware;
    bool _tgzupload_canFlash;
    File _tgzupload_rsrc;
    unsigned long _tgzupload_bytesWritten;
    std::vector<String> _tgzupload_filelist;
    bool _tgzupload_hasManifest;

    const char * _updater_errstr(uint8_t);

    void _listFilesWithPrefix(std::vector<String> &, const char *);
    void _deleteFilesWithPrefix(const char *);
    void _changeFileListPrefix(std::vector<String> &, const char *, const char *);
    void _loadManifest(std::vector<String> &);

    void _firmwareAbort(void);

public:
    YuboxOTA_Flasher_ESP32(
        YuboxOTA_Flasher_FileStart_func_cb filestart_cb,
        YuboxOTA_Flasher_FileProgress_func_cb fileprogress_cb,
        YuboxOTA_Flasher_FileEnd_func_cb fileend_cb
    );

    // Called in order to setup everything for receiving update chunks
    bool startUpdate(void);

    // Called when stream EOF has been encountered but tar data has not finished (truncated file)
    void truncateUpdate(void);

    // Called when tar data stream has finished properly
    bool finishUpdate(void);

    // Check if current update process cannot continue due to an unrecoverable error
    bool isUpdateRejected(void);

    // Get last error message from update process, or empty string on success
    String getLastErrorMessage(void);

    // Start new file entry from uncompressed update stream
    bool startFile(const char *, unsigned long long);

    // Append data to file entry opened with startFile()
    bool appendFileData(const char *, unsigned long long, unsigned char *, int);

    // Finish data on opened file entry
    bool finishFile(const char *, unsigned long long);

    // Check if this firmware update needs a reboot to be applied
    bool shouldReboot(void);

    bool canRollBack(void);

    bool doRollBack(void);

    void cleanupFailedUpdateFiles(void);
};

#endif