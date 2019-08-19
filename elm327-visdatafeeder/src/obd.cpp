/*
 * ******************************************************************************
 * Copyright (c) 2018 Robert Bosch GmbH and others.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * which accompanies this distribution, and is available at
 * https://www.eclipse.org/org/documents/epl-2.0/index.php
 *
 *  Contributors:
 *      Robert Bosch GmbH - initial API and functionality
 * *****************************************************************************
 */
#include "obd.hpp"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <unistd.h>
#include <iostream>

using namespace std;

// OBDII serial port setting.

// To change the baud rate on the kuksa-OBD-Dongle use the below commands and
// also modify the value below.
// The serial device is /dev/ttyAMA0
// Open using screen using the current baud rate -> screen /dev/ttyAMA0 <PRESENT
// BAUD RATE>
// You should see ">" prompt
// now type STSBR <NEW BAUD RATE> press enter and you should see OK on the
// screen.
// now type STWBR press enter and you should get an "OK" on the screen
// Close the screen window.
#define BAUD_RATE B38400 // Baudrate 38400
extern char PORT[128];

// OBDII connection handle.
int connectionHandle = -1;

// READ/WRITE mutex on Serial OBDII connection.
pthread_mutex_t obdMutex;

// filter out unwanted characters from raw data from the vehicle.
void filter(char* str, int size) {
  int index = 0;
  for (int i = 0; i < size; i++) {
    if (str[i] == '\r' || str[i] == '\n' || str[i] == ' ')
      continue;
    else if (str[i] == ':') {
      // TODO: this will work only for 1-9 blocks, but this suffices the present
      // requirement. This may cause a problem with Reading DTC, for
      // sensor values should not matter.
      index = index-1;
      continue;
    } else if (str[i] == '>') {
      str[index++] = '\0';
      return;
    }
    str[index++] = str[i];
  }
}

// Reset ELM327.
void resetELM() {
  string reset = "ATZ\r\n";

  int res = write(connectionHandle, (char*)reset.c_str(), 5);

  fsync(connectionHandle);
  usleep(50000);

  char read_buffer[64] = {0};
  char character;
  int bytes_read = 0;
  do {
     read(connectionHandle, &character, 1);
     read_buffer[bytes_read++] = character;
  } while (character != '>'); 

  filter(read_buffer, bytes_read);
  cout << "RESET OBDII respone = " << string(read_buffer) << endl;
}

// Set protocol on ELM327.
void setProtocol(int protocol) {
  // TODO : Needs to be extended for other protocols.
  // set protocol to automatic
  string setProt = "ATSP0\r\n";
  write(connectionHandle, (char*)setProt.c_str(), 7);
  fsync(connectionHandle);
  usleep(50000);
  char read_buffer[64] = {0};
  char character;
  int bytes_read = 0;
  do {
     read(connectionHandle, &character, 1);
     read_buffer[bytes_read++] = character;
  } while (character != '>'); 
  filter(read_buffer, bytes_read);
#ifdef DEBUG
  cout << "response for Setprotocol to automatic is " << string(read_buffer) << endl;
#endif
}


// Connect to OBD II
bool connectOBD(int timeout) {
  if (connectionHandle != -1) {
    cout << "Seems like a connection is already active !!" << endl;
    return false;
  }

  struct termios SerialPortSettings;
  memset(&SerialPortSettings, 0, sizeof(SerialPortSettings));

  // get the comm parameters.
  tcgetattr(connectionHandle, &SerialPortSettings);

  SerialPortSettings.c_iflag &= ~(INLCR | ICRNL);  // Dont touch NL &  CR on input
  SerialPortSettings.c_iflag |= IGNPAR | IGNBRK;   // Ignore fRaming errors on input | ignore break condition
  SerialPortSettings.c_oflag &= ~(OPOST | ONLCR | OCRNL); //disable Impl specific handling, ,disable CR to NL
  SerialPortSettings.c_cflag &= ~(PARENB | PARODD | CSTOPB | CSIZE | CRTSCTS); // do not enable flow control, disable parity checking
  SerialPortSettings.c_cflag |= CLOCAL | CREAD | CS8;  // Enable receiver
  SerialPortSettings.c_lflag &= ~(ICANON | ISIG );  // Disable cannonical mode,disable echo.
  SerialPortSettings.c_cc[VMIN] = 100;    // min characters in non cannonical read
  SerialPortSettings.c_cc[VTIME] = 100;  // wait for val/10 seconds between each byte received.

  connectionHandle = open(PORT, O_RDWR | O_NOCTTY | O_NDELAY);
  // set the comm parameters for ELM 327 with Bluetooth.
  cfsetispeed(&SerialPortSettings, BAUD_RATE);
  cfsetospeed(&SerialPortSettings, BAUD_RATE);

  if (connectionHandle == -1) {
    cout << " Error while opening the serial port connection to ELM 327" << endl;
    return false;
  } else {
    cout << " Connection to the port sucessful with val " << connectionHandle << endl;
  }

  tcsetattr(connectionHandle, TCSANOW, &SerialPortSettings);

  // Apply ELM 327 settings.
  fcntl(connectionHandle, F_SETFL, 0);

  pthread_mutex_lock(&obdMutex);
  resetELM();
  usleep(200000);
  setProtocol(0);
  pthread_mutex_unlock(&obdMutex);

  return true;
}

// Method to read OBDII Mode 1 values.
string readMode1Data(string command) {
  pthread_mutex_lock(&obdMutex);
  int res = write(connectionHandle, command.c_str(), command.length());
  //fsync(connectionHandle);
  char read_buffer[64] = {0};
  char character;
  int bytes_read = 0;
  do {
     read(connectionHandle, &character, 1);
     read_buffer[bytes_read++] = character;
     
  } while (character != '>'); 

  pthread_mutex_unlock(&obdMutex);

  filter(read_buffer, bytes_read);

  string response(read_buffer);
#ifdef DEBUG
  cout << "Sensor Data as string from vehicle =" << endl << response << endl;
#endif

  return response;
}

// Method to read OBDII Mode 3 values.
string readMode3Data() {
  pthread_mutex_lock(&obdMutex);
  char cmdBuf[3] = {'0', '3', '\r'};

  int res = write(connectionHandle, cmdBuf, 3);
  fsync(connectionHandle);
  char read_buffer[128] = {0};
  char character;
  int bytes_read = 0;
  do {
     read(connectionHandle, &character, 1);
     read_buffer[bytes_read++] = character;
  } while (character != '>'); 
   
#ifdef DEBUG
  cout << "Total bytes read =" << bytes_read << endl;
#endif
  pthread_mutex_unlock(&obdMutex);
  filter(read_buffer, bytes_read);
  string response(read_buffer);
#ifdef DEBUG
  cout << "Error Data as string from vehicle =" << endl << response << endl;
#endif
  return response;
}

// Method to write Mode 8 vallues to the (CAN) BUS.
string writeMode8Data(string command) {
  pthread_mutex_lock(&obdMutex);
  int res = write(connectionHandle, command.c_str(), command.length());
  char read_buffer[64] = {0};
  char character;
  int bytes_read = 0;
  do {
     read(connectionHandle, &character, 1);
     read_buffer[bytes_read++] = character;
  } while (character != '>'); 
  
  tcflush(connectionHandle, TCIOFLUSH);
  pthread_mutex_unlock(&obdMutex);

  filter(read_buffer, bytes_read);

  string response(read_buffer);
#ifdef DEBUG
  cout << "Response for mode 8 as string from vehicle =" << endl
       << response << endl;
#endif

  return response;
}

// Close OBDII connection
void closeConnection() { close(connectionHandle); }

//  Test method.
int testCommands(string command, char* response) {
  command = command + '\r';
  int length = command.length();
  int res = write(connectionHandle, (char*)command.c_str(), length);
  fsync(connectionHandle);

  usleep(500000);

#ifdef DEBUG
  if (res != length) {
    cout << "not all request bytes written to the buffer! bytes written = "
         << res << endl;
  }
#endif

  char read_buffer[64] = {0};
  char character;
  int bytes_read = 0;
  do {
     read(connectionHandle, &character, 1);
     read_buffer[bytes_read++] = character;
  } while (character != '>'); 

#ifdef DEBUG
  cout << "Total bytes read as response = " << bytes_read << endl;
#endif
  filter(response, bytes_read);
  return bytes_read;
}
