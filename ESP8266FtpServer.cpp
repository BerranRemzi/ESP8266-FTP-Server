/*
 * FTP Serveur for ESP8266
 * based on FTP Serveur for Arduino Due and Ethernet shield (W5100) or WIZ820io (W5200)
 * based on Jean-Michel Gallego's work
 * modified to work with esp8266 SDFS by David Paiva david@nailbuster.com
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

#include "ESP8266FtpServer.h"
#ifdef ESP8266
#include <ESP8266WiFi.h>
#elif defined ESP32
#include <WiFi.h>
#include "VirtualFS->h"
#endif
#include <WiFiClient.h>
#include <FS.h>
#include <SDFS.h>
#include <sdControl.h>

WiFiServer controlServer(FTP_CTRL_PORT);
WiFiServer dataServer(FTP_DATA_PORT_PASV);

static String EpochToISO(time_t epochTime)
{
  tm localTime;                        // the structure tm holds time information in a more convenient way
  localtime_r(&epochTime, &localTime); // update the structure tm with the current time

  // Format into YYYYMMDDHHMMSS
  char buffer[35];
  snprintf(buffer, sizeof(buffer), "%04d%02d%02d%02d%02d%02d",
           (1900 + localTime.tm_year),
           localTime.tm_mon,
           (1 + localTime.tm_mday),
           localTime.tm_hour,
           localTime.tm_min,
           localTime.tm_min);

  return String(buffer);
}

void FtpServer::addUser(String uname, String pword, int16_t pin)
{
  _user[_userIndex].name = uname;
  _user[_userIndex].password = pword;
  _user[_userIndex].pin = pin;

  if (NOT_A_PIN != _user[_userIndex].pin)
  {
    sdControl.setup(_user[_userIndex].pin);
  }

  _userIndex++;
}

void FtpServer::begin()
{
  // Tells the ftp server to begin listening for incoming connection
  controlServer.begin();
  delay(10);

  dataServer.begin();
  delay(10);

  millisTimeOut = (uint32_t)FTP_TIME_OUT * 60 * 1000;
  millisDelay = 0;
  cmdStatus = 0;
  iniVariables();

  configTime(MY_TZ, MY_NTP_SERVER);
}

void FtpServer::iniVariables()
{
  // Default for data port
  dataPort = FTP_DATA_PORT_PASV;

  // Default Data connection is Active
  dataPassiveConn = true;

  // Set the root directory
  strcpy(cwdName, "/");

  rnfrCmd = false;
  transferStatus = 0;
}

void FtpServer::handleFTP()
{
  typedef enum
  {
    DISCONNECTED = 0,
    WAIT_FOR_CONNECTION = 1,
    IDLE = 2,
    WAIT_FOR_USER_IDENTITY = 3,
    WAIT_FOR_USER_PASSWORD = 4,
    WAIT_FOR_USER_COMMAND = 5,
    COMMAND_STATUS_COUNT
  } CommandStatus_t;

  const String commandStatusString[COMMAND_STATUS_COUNT] = {
      "DISCONNECTED",
      "WAIT_FOR_CONNECTION",
      "WAIT_FOR_USER_IDENTITY",
      "WAIT_FOR_USER_PASSWORD",
      "WAIT_FOR_USER_COMMAND ",
  };

  typedef enum
  {
    NO_TRANSFER = 0,
    RETRIVE_DATA = 1,
    STORE_DATA = 2
  } TransferStatus_t;

  if ((int32_t)(millisDelay - millis()) > 0)
  {
    return;
  }

  if (controlServer.hasClient())
  {
    client.stop();
    client = controlServer.accept();
  }

  if (cmdStatus == DISCONNECTED)
  {
    if (client.connected())
    {
      disconnectClient();
    }
    cmdStatus = WAIT_FOR_CONNECTION;
  }
  else if (cmdStatus == WAIT_FOR_CONNECTION) // Ftp server waiting for connection
  {
    abortTransfer();
    iniVariables();
#ifdef FTP_DEBUG
    Serial.println("Ftp server waiting for connection on port " + String(FTP_CTRL_PORT));
#endif
    cmdStatus = IDLE;
  }
  else if (cmdStatus == IDLE) // Ftp server idle
  {
    if (client.connected()) // A client connected
    {
      clientConnected();
      millisEndConnection = millis() + 10 * 1000; // wait client id during 10 s.
      cmdStatus = WAIT_FOR_USER_IDENTITY;
    }
  }
  else if (readChar() > 0) // got response
  {
    if (cmdStatus == WAIT_FOR_USER_IDENTITY)
    { // Ftp server waiting for user identity
      if (userIdentity())
      {
        cmdStatus = WAIT_FOR_USER_PASSWORD;
      }
      else
      {
        cmdStatus = DISCONNECTED;
      }
    }
    else if (cmdStatus == WAIT_FOR_USER_PASSWORD)
    {
      // Ftp server waiting for user registration
      if (userPassword())
      {
        if (NOT_A_PIN != _user[_selectedUser].pin)
        {
          if (sdControl.takeBusControl())
          {
            // Init SD card
            VirtualFS = &SDFS;
            VirtualFS->begin();
          }
        }
        else
        {
          // Init SD card
          VirtualFS = &LittleFS;
          VirtualFS->begin();
        }
        cmdStatus = WAIT_FOR_USER_COMMAND;
        millisEndConnection = millis() + millisTimeOut;
      }
      else
      {
        cmdStatus = DISCONNECTED;
      }
    }
    else if (cmdStatus == WAIT_FOR_USER_COMMAND)
    { // Ftp server waiting for user command
      if (!processCommand())
      {
        cmdStatus = DISCONNECTED;
      }
      else
      {
        millisEndConnection = millis() + millisTimeOut;
      }
    }
  }
  else if (!client.connected() || !client)
  {
    cmdStatus = WAIT_FOR_CONNECTION;
#ifdef FTP_DEBUG
    Serial.println("client disconnected");
#endif
    // Release SD card
    VirtualFS->end();
    sdControl.releaseBusControl();
  }

  if (transferStatus == RETRIVE_DATA) // Retrieve data
  {
    if (!doRetrieve())
      transferStatus = NO_TRANSFER;
  }
  else if (transferStatus == STORE_DATA) // Store data
  {
    if (!doStore())
      transferStatus = NO_TRANSFER;
  }
  else if (cmdStatus > IDLE && !((int32_t)(millisEndConnection - millis()) > 0))
  {
    client.println("530 Timeout");
    millisDelay = millis() + 200; // delay of 200 ms
    cmdStatus = DISCONNECTED;
  }
}

void FtpServer::clientConnected()
{
#ifdef FTP_DEBUG
  Serial.println("Client connected!");
#endif
  client.println("220--- Welcome to FTP for ESP8266/ESP32 ---");
  client.println("220---   By David Paiva   ---");
  client.println("220 --   Version " + String(FTP_SERVER_VERSION) + "   --");
  iCL = 0;
}

void FtpServer::disconnectClient()
{
#ifdef FTP_DEBUG
  Serial.println(" Disconnecting client");
#endif
  abortTransfer();
  client.println("221 Goodbye");
  client.stop();
}

boolean FtpServer::userIdentity()
{
  if (strcmp(command, "USER"))
  {
    client.println("500 Syntax error");
  }

  _selectedUser = -1;
  for (uint8_t i = 0u; i < _userIndex; i++)
  {
    if (0 == strcmp(parameters, _user[i].name.c_str()))
    {
      _selectedUser = i;
      client.println("331 OK. Password required");
      strcpy(cwdName, "/");
      return true;
    }
  }

  client.println("530 user not found");

  millisDelay = millis() + 100; // delay of 100 ms
  return false;
}

boolean FtpServer::userPassword()
{
  if (strcmp(command, "PASS"))
  {
    client.println("500 Syntax error");
  }
  else if (strcmp(parameters, _user[_selectedUser].password.c_str()))
  {
    client.println("530 ");
  }
  else
  {
#ifdef FTP_DEBUG
    Serial.println("OK. Waiting for commands.");
#endif
    client.println("230 OK.");
    return true;
  }
  millisDelay = millis() + 100; // delay of 100 ms
  return false;
}

///////////////////////////////////////
//                                   //
//      ACCESS CONTROL COMMANDS      //
//                                   //
///////////////////////////////////////

//
//  CDUP - Change to Parent Directory
//
bool FtpServer::command_CDUP()
{
  client.println("250 Ok. Current directory is " + String(cwdName));
  return true;
}

//
//  CWD - Change Working Directory
//
bool FtpServer::command_CWD()
{
  char path[FTP_CWD_SIZE];
  if (strcmp(parameters, ".") == 0)
  { // 'CWD .' is the same as PWD command
    client.println("257 \"" + String(cwdName) + "\" is your current directory");
  }
  else
  {
    strcpy(cwdName, parameters);
    client.println("250 Ok. Current directory is " + String(cwdName));
  }
  return true;
}

//
//  PWD - Print Directory
//
bool FtpServer::command_PWD()
{
  client.println("257 \"" + String(cwdName) + "\" is your current directory");
  return true;
}
//
//  QUIT
//
bool FtpServer::command_QUIT()
{
  disconnectClient();
  return false;
}

///////////////////////////////////////
//                                   //
//    TRANSFER PARAMETER COMMANDS    //
//                                   //
///////////////////////////////////////

//
//  MODE - Transfer Mode
//
bool FtpServer::command_MODE()
{
  if (!strcmp(parameters, "S"))
  {
    client.println("200 S Ok");
  }
  // else if( ! strcmp( parameters, "B" ))
  //  client.println( "200 B Ok\r\n";
  else
  {
    client.println("504 Only S(tream) is suported");
  }
  return true;
}
//
//  PASV - Passive Connection management
//
bool FtpServer::command_PASV()
{
  if (data.connected())
  {
    data.stop();
  }
  // dataServer.begin();
  // dataIp = Ethernet.localIP();
  dataIp = client.localIP();
  dataPort = FTP_DATA_PORT_PASV;
// data.connect( dataIp, dataPort );
// data = dataServer.available();
#ifdef FTP_DEBUG
  Serial.println("Connection management set to passive");
  Serial.println("Data port set to " + String(dataPort));
#endif
  client.println("227 Entering Passive Mode (" + String(dataIp[0]) + "," + String(dataIp[1]) + "," + String(dataIp[2]) + "," + String(dataIp[3]) + "," + String(dataPort >> 8) + "," + String(dataPort & 255) + ").");
  dataPassiveConn = true;
  return true;
}
//
//  PORT - Data Port
//
bool FtpServer::command_PORT()
{
  if (data)
  {
    data.stop();
  }
  // get IP of data client
  dataIp[0] = atoi(parameters);
  char *p = strchr(parameters, ',');
  for (uint8_t i = 1; i < 4; i++)
  {
    dataIp[i] = atoi(++p);
    p = strchr(p, ',');
  }
  // get port of data client
  dataPort = 256 * atoi(++p);
  p = strchr(p, ',');
  dataPort += atoi(++p);
  if (p == NULL)
  {
    client.println("501 Can't interpret parameters");
  }
  else
  {

    client.println("200 PORT command successful");
    dataPassiveConn = false;
  }
  return true;
}
//
//  STRU - File Structure
//
bool FtpServer::command_STRU()
{
  if (!strcmp(parameters, "F"))
  {
    client.println("200 F Ok");
  }
  // else if( ! strcmp( parameters, "R" ))
  //  client.println( "200 B Ok\r\n";
  else
  {
    client.println("504 Only F(ile) is suported");
  }
  return true;
}
//
//  TYPE - Data Type
//
bool FtpServer::command_TYPE()
{
  if (!strcmp(parameters, "A"))
  {
    client.println("200 TYPE is now ASII");
  }
  else if (!strcmp(parameters, "I"))
  {
    client.println("200 TYPE is now 8-bit binary");
  }
  else
  {
    client.println("504 Unknow TYPE");
  }
  return true;
}

///////////////////////////////////////
//                                   //
//        FTP SERVICE COMMANDS       //
//                                   //
///////////////////////////////////////

//
//  ABOR - Abort
//
bool FtpServer::command_ABOR()
{
  abortTransfer();
  client.println("226 Data connection closed");
  return true;
}
//
//  DELE - Delete a File
//
bool FtpServer::command_DELE()
{
  char path[FTP_CWD_SIZE];
  if (strlen(parameters) == 0)
  {
    client.println("501 No file name");
  }
  else if (makePath(path))
  {
    if (!VirtualFS->exists(path))
    {
      client.println("550 File " + String(parameters) + " not found");
    }
    else
    {
      if (VirtualFS->remove(path))
        client.println("250 Deleted " + String(parameters));
      else
        client.println("450 Can't delete " + String(parameters));
    }
  }
  return true;
}
//
//  LIST - List
//
bool FtpServer::command_LIST()
{
  if (!dataConnect())
  {
    client.println("425 No data connection");
  }
  else
  {
    client.println("150 Accepted data connection");
    uint16_t nm = 0;
#ifdef ESP8266
    Dir dir = VirtualFS->openDir(cwdName);
    if (!VirtualFS->exists(cwdName))
    {
      client.println("550 Can't open directory " + String(cwdName));
    }
    else
    {
      while (dir.next())
      {
        String fn, fs;

        fn = dir.fileName();
        // fn.remove(0, 1);
        fs = String(dir.fileSize());
        data.println("+r,s" + fs);
        data.println(",\t" + fn);
        nm++;
      }
      client.println("226 " + String(nm) + " matches total");
    }
#elif defined ESP32
    File root = VirtualFS->open(cwdName);
    if (!root)
    {
      client.println("550 Can't open directory " + String(cwdName));
      // return;
    }
    else
    {
      // if(!root.isDirectory()){
      // 		Serial.println("Not a directory");
      // 		return;
      // }

      File file = root.openNextFile();
      while (file)
      {
        if (file.isDirectory())
        {
          data.println("+r,s <DIR> " + String(file.name()));
          // Serial.print("  DIR : ");
          // Serial.println(file.name());
          // if(levels){
          // 	listDir(fs, file.name(), levels -1);
          // }
        }
        else
        {
          String fn, fs;
          fn = file.name();
          // fn.remove(0, 1);
          fs = String(file.size());
          data.println("+r,s" + fs);
          data.println(",\t" + fn);
          nm++;
        }
        file = root.openNextFile();
      }
      client.println("226 " + String(nm) + " matches total");
    }
#endif
    data.stop();
  }
  return true;
}
//
//  MLSD - Listing for Machine Processing (see RFC 3659)
//
bool FtpServer::command_MLSD()
{
  if (!dataConnect())
  {
    client.println("425 No data connection MLSD");
  }
  else
  {
    client.println("150 Accepted data connection");
    uint16_t nm = 0;
#ifdef ESP8266
    Dir dir = VirtualFS->openDir(cwdName);
    Serial.println(cwdName);
    char dtStr[15];
    if (!VirtualFS->exists(cwdName))
    {
      client.println("550 Can't open directory " + String(parameters));
    }
    else
    {
      while (dir.next())
      {
        String fn = dir.fileName();
        String type = dir.isDirectory() ? "dir" : "file";
        String fs = type == "dir" ? "0" : String(dir.fileSize());

        String modify = type == "dir" ? EpochToISO(dir.fileCreationTime()) : EpochToISO(dir.fileTime());
        data.println("Type=" + type + ";Size=" + fs + ";modify=" + modify + "; " + fn);
        nm++;
      }
      client.println("226-options: -a -l");
      client.println("226 " + String(nm) + " matches total");
    }
#elif defined ESP32
    File root = VirtualFS->open(cwdName);
    // if(!root){
    // 		client.println( "550 Can't open directory " + String(cwdName) );
    // 		// return;
    // } else {
    // if(!root.isDirectory()){
    // 		Serial.println("Not a directory");
    // 		return;
    // }

    File file = root.openNextFile();
    while (file)
    {
      // if(file.isDirectory()){
      // 	data.println( "+r,s <DIR> " + String(file.name()));
      // 	// Serial.print("  DIR : ");
      // 	// Serial.println(file.name());
      // 	// if(levels){
      // 	// 	listDir(fs, file.name(), levels -1);
      // 	// }
      // } else {
      String fn, fs;
      fn = file.name();
      fn.remove(0, 1);
      fs = String(file.size());
      data.println("Type=file;Size=" + fs + ";" + "modify=20000101160656;" + " " + fn);
      nm++;
      // }
      file = root.openNextFile();
    }
    client.println("226-options: -a -l");
    client.println("226 " + String(nm) + " matches total");
    // }
#endif
    data.stop();
  }
  return true;
}
//
//  NLST - Name List
//
bool FtpServer::command_NLST()
{
  if (!dataConnect())
  {
    client.println("425 No data connection");
  }
  else
  {
    client.println("150 Accepted data connection");
    uint16_t nm = 0;
#ifdef ESP8266
    Dir dir = VirtualFS->openDir(cwdName);
    if (!VirtualFS->exists(cwdName))
      client.println("550 Can't open directory " + String(parameters));
    else
    {
      while (dir.next())
      {
        data.println(dir.fileName());
        nm++;
      }
      client.println("226 " + String(nm) + " matches total");
    }
#elif defined ESP32
    File root = VirtualFS->open(cwdName);
    if (!root)
    {
      client.println("550 Can't open directory " + String(cwdName));
    }
    else
    {

      File file = root.openNextFile();
      while (file)
      {
        data.println(file.name());
        nm++;
        file = root.openNextFile();
      }
      client.println("226 " + String(nm) + " matches total");
    }
#endif
    data.stop();
  }
  return true;
}
//
//  NOOP
//
bool FtpServer::command_NOOP()
{
  // dataPort = 0;
  client.println("200 Zzz...");
  return true;
}
//
//  RETR - Retrieve
//
bool FtpServer::command_RETR()
{
  char path[FTP_CWD_SIZE];
  if (strlen(parameters) == 0)
    client.println("501 No file name");
  else if (makePath(path))
  {
    file = VirtualFS->open(path, "r");
    if (!file)
      client.println("550 File " + String(parameters) + " not found");
    else if (!file)
      client.println("450 Can't open " + String(parameters));
    else if (!dataConnect())
      client.println("425 No data connection");
    else
    {
#ifdef FTP_DEBUG
      Serial.println("Sending " + String(parameters));
#endif
      client.println("150-Connected to port " + String(dataPort));
      client.println("150 " + String(file.size()) + " bytes to download");
      millisBeginTrans = millis();
      bytesTransfered = 0;
      transferStatus = 1;
    }
  }
  return true;
}
//
//  STOR - Store
//
bool FtpServer::command_STOR()
{
  char path[FTP_CWD_SIZE];
  if (strlen(parameters) == 0)
    client.println("501 No file name");
  else if (makePath(path))
  {
    file = VirtualFS->open(path, "w");
    if (!file)
      client.println("451 Can't open/create " + String(parameters));
    else if (!dataConnect())
    {
      client.println("425 No data connection");
      file.close();
    }
    else
    {
#ifdef FTP_DEBUG
      Serial.println("Receiving " + String(parameters));
#endif
      client.println("150 Connected to port " + String(dataPort));
      millisBeginTrans = millis();
      bytesTransfered = 0;
      transferStatus = 2;
    }
  }
  return true;
}
//
//  MKD - Make Directory
//
bool FtpServer::command_MKD()
{
  client.println("550 Can't create \"" + String(parameters)); // not support on espyet
  return true;
}
//
//  RMD - Remove a Directory
//
bool FtpServer::command_RMD()
{
  client.println("501 Can't delete \"" + String(parameters));
  return true;
}
//
//  RNFR - Rename From
//
bool FtpServer::command_RNFR()
{
  buf[0] = 0;
  if (strlen(parameters) == 0)
  {
    client.println("501 No file name");
  }
  else if (makePath(buf))
  {
    if (!VirtualFS->exists(buf))
    {
      client.println("550 File " + String(parameters) + " not found");
    }
    else
    {
#ifdef FTP_DEBUG
      Serial.println("Renaming " + String(buf));
#endif
      client.println("350 RNFR accepted - file exists, ready for destination");
      rnfrCmd = true;
    }
  }
  return true;
}
//
//  RNTO - Rename To
//
bool FtpServer::command_RNTO()
{
  char path[FTP_CWD_SIZE];
  char dir[FTP_FIL_SIZE];
  if (strlen(buf) == 0 || !rnfrCmd)
    client.println("503 Need RNFR before RNTO");
  else if (strlen(parameters) == 0)
    client.println("501 No file name");
  else if (makePath(path))
  {
    if (VirtualFS->exists(path))
      client.println("553 " + String(parameters) + " already exists");
    else
    {
#ifdef FTP_DEBUG
      Serial.println("Renaming " + String(buf) + " to " + String(path));
#endif
      if (VirtualFS->rename(buf, path))
        client.println("250 File successfully renamed or moved");
      else
        client.println("451 Rename/move failure");
    }
  }
  rnfrCmd = false;
  return true;
}

///////////////////////////////////////
//                                   //
//   EXTENSIONS COMMANDS (RFC 3659)  //
//                                   //
///////////////////////////////////////

//
//  FEAT - New Features
//

bool FtpServer::command_FEAT()
{
  client.println("211-Extensions suported:");
  client.println(" MLSD");
  client.println("211 End.");
  return true;
}
//
//  MDTM - File Modification Time (see RFC 3659)
//
bool FtpServer::command_MDTM()
{
  client.println("550 Unable to retrieve time");
  return true;
}
//
//  SIZE - Size of the file
//
bool FtpServer::command_SIZE()
{
  char path[FTP_CWD_SIZE];
  if (strlen(parameters) == 0)
  {
    client.println("501 No file name");
  }
  else if (makePath(path))
  {
    file = VirtualFS->open(path, "r");
    if (!file)
    {
      client.println("450 Can't open " + String(parameters));
    }
    else
    {
      client.println("213 " + String(file.size()));
      file.close();
    }
  }
  return true;
}
//
//  SITE - System command
//
bool FtpServer::command_SITE()
{
  client.println("500 Unknow SITE command " + String(parameters));
  return true;
}

//
//  Unrecognized commands ...
//
bool FtpServer::command_Unrecognized()
{
  client.println("500 Unknow command");
  return true;
}

boolean FtpServer::processCommand()
{
  ///////////////////////////////////////
  //                                   //
  //      ACCESS CONTROL COMMANDS      //
  //                                   //
  ///////////////////////////////////////
  typedef bool (FtpServer::*CommandHandler)();
  typedef struct
  {
    const char *name;
    CommandHandler handler;
  } Command_t;

  const Command_t commandTable[25] = {
      {"CDUP", &FtpServer::command_CDUP},
      {"CWD", &FtpServer::command_CWD},
      {"PWD", &FtpServer::command_PWD},
      {"QUIT", &FtpServer::command_QUIT},
      {"MODE", &FtpServer::command_MODE},
      {"PASV", &FtpServer::command_PASV},
      {"PORT", &FtpServer::command_PORT},
      {"STRU", &FtpServer::command_STRU},
      {"TYPE", &FtpServer::command_TYPE},
      {"ABOR", &FtpServer::command_ABOR},
      {"DELE", &FtpServer::command_DELE},
      {"LIST", &FtpServer::command_LIST},
      {"MLSD", &FtpServer::command_MLSD},
      {"NLST", &FtpServer::command_NLST},
      {"NOOP", &FtpServer::command_NOOP},
      {"RETR", &FtpServer::command_RETR},
      {"STOR", &FtpServer::command_STOR},
      {"MKD", &FtpServer::command_MKD},
      {"RMD", &FtpServer::command_RMD},
      {"RNFR", &FtpServer::command_RNFR},
      {"RNTO", &FtpServer::command_RNTO},
      {"FEAT", &FtpServer::command_FEAT},
      {"MDTM", &FtpServer::command_MDTM},
      {"SIZE", &FtpServer::command_SIZE},
      {"SITE", &FtpServer::command_SITE},
  };

  for (const Command_t &cmd : commandTable)
  {
    if (cmd.name && !strcmp(command, cmd.name))
    {
      return (this->*(cmd.handler))();
    }
  }
  return command_Unrecognized();
}

boolean FtpServer::dataConnect()
{
  unsigned long startTime = millis();
  // wait 5 seconds for a data connection
  if (!data.connected())
  {
    while (!dataServer.hasClient() && millis() - startTime < 10000)
    {
      // delay(100);
      yield();
    }
    if (dataServer.hasClient())
    {
      data.stop();
      data = dataServer.accept();
#ifdef FTP_DEBUG
      Serial.println("ftpdataserver client....");
#endif
    }
  }

  return data.connected();
}

boolean FtpServer::doRetrieve()
{
  if (data.connected())
  {
    int16_t nb = file.readBytes(buf, FTP_BUF_SIZE);
    if (nb > 0)
    {
      data.write((uint8_t *)buf, nb);
      bytesTransfered += nb;
      return true;
    }
  }
  closeTransfer();
  return false;
}

boolean FtpServer::doStore()
{
  // Avoid blocking by never reading more bytes than are available
  int navail = data.available();

  if (navail > 0)
  {
    // And be sure not to overflow buf.
    if (navail > FTP_BUF_SIZE)
      navail = FTP_BUF_SIZE;
    int16_t nb = data.read((uint8_t *)buf, navail);
    // int16_t nb = data.readBytes((uint8_t*) buf, FTP_BUF_SIZE );
    if (nb > 0)
    {
      // Serial.println( millis() << " " << nb << endl;
      file.write((uint8_t *)buf, nb);
      bytesTransfered += nb;
    }
  }
  if (!data.connected() && (navail <= 0))
  {
    closeTransfer();
    return false;
  }
  else
  {
    return true;
  }
}

void FtpServer::closeTransfer()
{
  uint32_t deltaT = (int32_t)(millis() - millisBeginTrans);
  if (deltaT > 0 && bytesTransfered > 0)
  {
    client.println("226-File successfully transferred");
    client.println("226 " + String(deltaT) + " ms, " + String(bytesTransfered / deltaT) + " kbytes/s");
  }
  else
    client.println("226 File successfully transferred");

  file.close();
  data.stop();
}

void FtpServer::abortTransfer()
{
  if (transferStatus > 0)
  {
    file.close();
    data.stop();
    client.println("426 Transfer aborted");
#ifdef FTP_DEBUG
    Serial.println("Transfer aborted!");
#endif
  }
  transferStatus = 0;
}

// Read a char from client connected to ftp server
//
//  update cmdLine and command buffers, iCL and parameters pointers
//
//  return:
//    -2 if buffer cmdLine is full
//    -1 if line not completed
//     0 if empty line received
//    length of cmdLine (positive) if no empty line received

int8_t FtpServer::readChar()
{
  int8_t rc = -1;

  if (client.available())
  {
    char c = client.read();
    // char c;
    // client.readBytes((uint8_t*) c, 1);
#ifdef FTP_DEBUG
    Serial.print(c);
#endif
    if (c == '\\')
    {
      c = '/';
    }
    if (c != '\r')
    {
      if (c != '\n')
      {
        if (iCL < FTP_CMD_SIZE)
          cmdLine[iCL++] = c;
        else
          rc = -2; //  Line too long
      }
      else
      {
        cmdLine[iCL] = 0;
        command[0] = 0;
        parameters = NULL;
        // empty line?
        if (iCL == 0)
          rc = 0;
        else
        {
          rc = iCL;
          // search for space between command and parameters
          parameters = strchr(cmdLine, ' ');
          if (parameters != NULL)
          {
            if (parameters - cmdLine > 4)
              rc = -2; // Syntax error
            else
            {
              strncpy(command, cmdLine, parameters - cmdLine);
              command[parameters - cmdLine] = 0;

              while (*(++parameters) == ' ')
                ;
            }
          }
          else if (strlen(cmdLine) > 4)
            rc = -2; // Syntax error.
          else
            strcpy(command, cmdLine);
          iCL = 0;
        }
      }
    }
    if (rc > 0)
      for (uint8_t i = 0; i < strlen(command); i++)
        command[i] = toupper(command[i]);
    if (rc == -2)
    {
      iCL = 0;
      client.println("500 Syntax error");
    }
  }
  return rc;
}

// Make complete path/name from cwdName and parameters
//
// 3 possible cases: parameters can be absolute path, relative path or only the name
//
// parameters:
//   fullName : where to store the path/name
//
// return:
//    true, if done

boolean FtpServer::makePath(char *fullName)
{
  return makePath(fullName, parameters);
}

boolean FtpServer::makePath(char *fullName, char *param)
{
  if (param == NULL)
    param = parameters;

  // Root or empty?
  if (strcmp(param, "/") == 0 || strlen(param) == 0)
  {
    strcpy(fullName, "/");
    return true;
  }
  // If relative path, concatenate with current dir
  if (param[0] != '/')
  {
    strcpy(fullName, cwdName);
    if (fullName[strlen(fullName) - 1] != '/')
      strncat(fullName, "/", FTP_CWD_SIZE);
    strncat(fullName, param, FTP_CWD_SIZE);
  }
  else
    strcpy(fullName, param);
  // If ends with '/', remove it
  uint16_t strl = strlen(fullName) - 1;
  if (fullName[strl] == '/' && strl > 1)
    fullName[strl] = 0;
  if (strlen(fullName) < FTP_CWD_SIZE)
    return true;

  client.println("500 Command line too long");
  return false;
}

// Calculate year, month, day, hour, minute and second
//   from first parameter sent by MDTM command (YYYYMMDDHHMMSS)
//
// parameters:
//   pyear, pmonth, pday, phour, pminute and psecond: pointer of
//     variables where to store data
//
// return:
//    0 if parameter is not YYYYMMDDHHMMSS
//    length of parameter + space

uint8_t FtpServer::getDateTime(uint16_t *pyear, uint8_t *pmonth, uint8_t *pday,
                               uint8_t *phour, uint8_t *pminute, uint8_t *psecond)
{
  char dt[15];

  // Date/time are expressed as a 14 digits long string
  //   terminated by a space and followed by name of file
  if (strlen(parameters) < 15 || parameters[14] != ' ')
    return 0;
  for (uint8_t i = 0; i < 14; i++)
    if (!isdigit(parameters[i]))
      return 0;

  strncpy(dt, parameters, 14);
  dt[14] = 0;
  *psecond = atoi(dt + 12);
  dt[12] = 0;
  *pminute = atoi(dt + 10);
  dt[10] = 0;
  *phour = atoi(dt + 8);
  dt[8] = 0;
  *pday = atoi(dt + 6);
  dt[6] = 0;
  *pmonth = atoi(dt + 4);
  dt[4] = 0;
  *pyear = atoi(dt);
  return 15;
}

uint32_t sntp_startup_delay_MS_rfc_not_less_than_60000()
{
  return 5000u;
}
