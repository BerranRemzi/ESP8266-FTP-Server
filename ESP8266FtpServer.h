
/*
 *  FTP SERVER FOR ESP8266
 * based on FTP Serveur for Arduino Due and Ethernet shield (W5100) or WIZ820io (W5200)
 * based on Jean-Michel Gallego's work
 * modified to work with esp8266 SPIFFS by David Paiva (david@nailbuster.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*******************************************************************************
 **                                                                            **
 **                       DEFINITIONS FOR FTP SERVER                           **
 **                                                                            **
 *******************************************************************************/

// Uncomment to print debugging info to console attached to ESP8266
#define FTP_DEBUG

#ifndef FTP_SERVERESP_H
#define FTP_SERVERESP_H

// #include "Streaming.h"
#include <FS.h>
#include <WiFiClient.h>
#include <LittleFS.h>
#include <SDFS.h>
#include <time.h>

/* Configuration of NTP */
#define MY_NTP_SERVER "bg.pool.ntp.org"
#define MY_TZ "UTC0"

#define FTP_SERVER_VERSION "FTP-2017-10-18"

#define FTP_CTRL_PORT 21         // Command port on wich server is listening
#define FTP_DATA_PORT_PASV 50009 // Data port in passive mode

#define FTP_TIME_OUT 5       // Disconnect client after 5 minutes of inactivity
#define FTP_CMD_SIZE 255 + 8 // max size of a command
#define FTP_CWD_SIZE 255 + 8 // max size of a directory name
#define FTP_FIL_SIZE 255     // max size of a file name
// #define FTP_BUF_SIZE 1024 //512   // size of file buffer for read/write
#define FTP_BUF_SIZE 2 * 1460 // 512   // size of file buffer for read/write

#define FTP_USER_COUNT 3u

typedef enum
{
  SD_IDLE,
  SD_BUSY,
  SD_MODE_COUNТ
} SDMode_t;

typedef struct
{
  String name;
  String password;
  int16_t pin;
} User_t;

class FtpServer
{
public:
  FtpServer()
  {
    _userIndex = 0u;
  }
  void addUser(String uname, String pword, int16_t pin = NOT_A_PIN);
  void begin();
  void handleFTP();

private:
  void iniVariables();
  void clientConnected();
  void disconnectClient();
  boolean userIdentity();
  boolean userPassword();
  boolean processCommand();
  boolean dataConnect();
  boolean doRetrieve();
  boolean doStore();
  void closeTransfer();
  void abortTransfer();
  boolean makePath(char *fullname);
  boolean makePath(char *fullName, char *param);
  uint8_t getDateTime(uint16_t *pyear, uint8_t *pmonth, uint8_t *pday,
                      uint8_t *phour, uint8_t *pminute, uint8_t *second);
  int8_t readChar();
  FS *VirtualFS = nullptr;
  IPAddress dataIp; // IP address of client for data
  WiFiClient client;
  WiFiClient data;

  File file;

  boolean dataPassiveConn;
  uint16_t dataPort;
  char buf[FTP_BUF_SIZE];     // data buffer for transfers
  char cmdLine[FTP_CMD_SIZE]; // where to store incoming char from client
  char cwdName[FTP_CWD_SIZE]; // name of current directory
  char command[5];            // command sent by client
  boolean rnfrCmd;            // previous command was RNFR
  char *parameters;           // point to begin of parameters sent by client
  uint16_t iCL;               // pointer to cmdLine next incoming char
  int8_t cmdStatus,           // status of ftp command connexion
      transferStatus;         // status of ftp data transfer
  uint32_t millisTimeOut,     // disconnect after 5 min of inactivity
      millisDelay,
      millisEndConnection, //
      millisBeginTrans,    // store time of beginning of a transaction
      bytesTransfered;     //

  User_t _user[FTP_USER_COUNT];
  uint8_t _userIndex = 0u;
  int8_t _selectedUser = -1;
  int16_t _sdCSPin = 5;

  bool command_CDUP();
  bool command_CWD();
  bool command_PWD();
  bool command_QUIT();
  bool command_MODE();
  bool command_PASV();
  bool command_PORT();
  bool command_STRU();
  bool command_TYPE();
  bool command_ABOR();
  bool command_DELE();
  bool command_LIST();
  bool command_MLSD();
  bool command_NLST();
  bool command_NOOP();
  bool command_RETR();
  bool command_STOR();
  bool command_MKD();
  bool command_RMD();
  bool command_RNFR();
  bool command_RNTO();
  bool command_FEAT();
  bool command_MDTM();
  bool command_SIZE();
  bool command_SITE();
  bool command_Unrecognized();
};

#endif // FTP_SERVERESP_H
