# ESP8266/ESP32 FTP Server

This project provides a simple FTP server for the ESP8266/ESP32, supporting both SPIFFS and SDFS file systems, with user-specific access control based on credentials.

### Features:

-   **User-Specific File System Access**:
    -   One user can access the SD card via SDFS.
    -   Another user can access the internal SPIFFS.
-   **File Operations**: Supports basic file operations such as upload, download, rename, and delete.
-   **Last Modified Time/Date**: The FTP server now supports retrieving and displaying the last modified time and date of files.
-   **ESP32 Compatibility**: This server now supports both ESP8266 and ESP32.
-   **Single FTP Connection**: For simplicity, only one FTP connection is allowed at a time.
-   **Passive FTP Mode**: The server operates in passive FTP mode only.

### Limitations:

-   No support for creating or modifying directories (SPIFFS currently lacks directory support).
-   No encryption support—FTP connections are unencrypted. Please ensure encryption is disabled in your client.

### Tested With:

The server has been tested with FileZilla. To ensure proper functionality, configure FileZilla (or any other FTP client) to allow only one connection at a time:

1.  In FileZilla, go to **File** > **Site Manager**, then select your site.
2.  In **Transfer Settings**, check "Limit number of simultaneous connections" and set the maximum to 1.

### Original Project:

This project is based on the original FTP server for the Arduino/WiFi shield: [Arduino FTP Server by gallegojm](https://github.com/gallegojm/Arduino-Ftp-Server/tree/master/FtpServer).

Feel free to try it out and modify as needed!
