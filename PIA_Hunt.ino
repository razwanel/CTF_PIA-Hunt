#include "BluetoothSerial.h"
#include "CRC.h"
#include "string.h"

#define TEAM_NUMBER 1007
#define SECRET_PASSWORD 555
#define MAX_INPUT_BUFFER 48

enum BT_STATUS
{
  CONNECT_NONE = 0,
  CONNECT_OK,
  CONNECT_FAIL,
  SEND_QUERRY,
  WAIT_FOR_AUTHORIZE,
  CHECK_AUTHORIZE,
  SEND_REPLY,
  WAIT_FOR_CONFIRMATION,
  CHECK_CONFIRMATION,
  FOUND_NUMBER,
};

String name = "PHNT2";

enum BT_STATUS status;
BluetoothSerial ESP_BT;
char authorize_pattern[]="210 Authorize ";
char confirmation_pattern[]="211 Confirmation code is:";
char reply[16];
bool connected;
char debug_input_line [MAX_INPUT_BUFFER];
unsigned int debug_input_pos = 0;
char input_line [MAX_INPUT_BUFFER];
unsigned int input_pos = 0;
unsigned int espected_rx_bytes;
int R;

void callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  if(event == ESP_SPP_OPEN_EVT){
    Serial.println("Client connected to server");
    connected = true;
    status = CONNECT_OK;
  }
    if(event == ESP_SPP_CLOSE_EVT ){
      /*Serial.printf("Received %d chars over BT\n",input_pos);
      if (input_pos != 0)
      {
        input_line [input_pos] = 0; // terminating null byte
        Serial.println(input_line);
      }*/
      Serial.println("Client disconnected from server");
      Serial.println("Connection lost! Make sure remote device is available and in range, then restart app.");
      connected = false;
      //status = CONNECT_FAIL;
  }
}

void processIncomingByte (const byte inByte, BT_STATUS nextStatus) {

  switch (inByte)
  {
    case '\r': // discard carriage return
    case '\n': // discard line feed
      break;
    default:
      // keep adding if not full ... allow for terminating null byte
      if (input_pos < (MAX_INPUT_BUFFER - 1))
        input_line [input_pos++] = inByte;
      if (input_pos == espected_rx_bytes)
      {
        input_line [input_pos] = 0; // terminating null byte
        status = nextStatus;
        input_pos = 0;
      }
      break;
  }
  
  switch (inByte)
  {
    case '\r': // discard carriage return
      break;
    case '\n': // line feed -> end of message
      debug_input_line [debug_input_pos] = 0; // terminating null byte
      debug_input_pos = 0;
      Serial.print("BT>");
      Serial.println(debug_input_line);
      break;
    default:
      // keep adding if not full ... allow for terminating null byte
      if (debug_input_pos < (MAX_INPUT_BUFFER - 1))
        debug_input_line [debug_input_pos++] = inByte;
      break;
  }

}


void setup() {
  Serial.begin(115200);
  ESP_BT.begin("ESP32_1007", true);
  Serial.println("The device started in master (client) mode, make sure remote BT device is on!");
  ESP_BT.register_callback(callback); // for connect/disconnect detection  
  connected = ESP_BT.connect(name); // connect using BT server name
  if(connected) {
    Serial.printf("Connected to %s Succesfully!\r\n", name.c_str());
    status = CONNECT_OK;
  } else {
    while(!ESP_BT.connected(10000)) {
      Serial.println("Failed to connect. Make sure remote device is available and in range, then restart app.");
      status = CONNECT_FAIL;      
    }
  }
}

void loop() {
  switch (status) {
    case CONNECT_OK:
      char NNNN[5];
      itoa(TEAM_NUMBER, NNNN, 10);
      strcpy(reply, "AT+U");
      strcat(reply, NNNN);
      reply[8]='\n';
      reply[9] = 0; // end of string    
      status = SEND_QUERRY;
      break;
    case SEND_QUERRY:
      input_pos = 0;
      espected_rx_bytes = 17; // reply of type "210 Authorize XXX"
      ESP_BT.write((const uint8_t *) reply, strlen(reply));
      Serial.print("C>");
      Serial.print(reply);
      status = WAIT_FOR_AUTHORIZE;
      break;
    case WAIT_FOR_AUTHORIZE:
      while (ESP_BT.available())
        processIncomingByte(ESP_BT.read(), CHECK_AUTHORIZE);
      break;
    case CHECK_AUTHORIZE:
      Serial.print("S>");
      Serial.println(input_line);
      if (strncmp(input_line, authorize_pattern, strlen(authorize_pattern)) == 0)
      {
        // found pattern,lets extract XXX number
        char XXX[4];
        strncpy(XXX, &input_line[strlen(authorize_pattern)], 4);
        int X = atoi(XXX);
        int Y = TEAM_NUMBER + X + SECRET_PASSWORD;
        char YYYY[5];
        itoa(Y, YYYY, 10);
        uint16_t crc=crc16((uint8_t *) YYYY, 4, 0x1021, 0, 0, false, false);
        char CCCCC[6];
        //itoa(crc, CCCCC, 10);
        sprintf(CCCCC, "%05d", crc);
        strcpy(reply, YYYY);
        strcat(reply, CCCCC);
        reply[9]='\n';
        reply[10] = 0; // end of string
        status = SEND_REPLY;
      }
      else
      {
        Serial.println("Couldn't find 210 Authorize text in reply, restart app!");
        status = CONNECT_NONE;
      }
      break;    
    case SEND_REPLY:
      input_pos = 0;
      espected_rx_bytes = 31; // reply of type "211 Confirmation code is:RRRRRR"
      ESP_BT.write((const uint8_t *) reply, strlen(reply));
      Serial.print("C>");
      Serial.print(reply);
      status = WAIT_FOR_CONFIRMATION;
      break;
    case WAIT_FOR_CONFIRMATION:
      while (ESP_BT.available())
        processIncomingByte(ESP_BT.read(), CHECK_CONFIRMATION);
      break;
    case CHECK_CONFIRMATION:
      Serial.print("S>");
      Serial.println(input_line);
      if (strncmp(input_line, confirmation_pattern, strlen(confirmation_pattern)) == 0)
      {
        // found pattern,lets extract RRRRRRR number
        char RRRRRR[7];
        strncpy(RRRRRR, &input_line[strlen(confirmation_pattern)], 7);
        R = atoi(RRRRRR);
        status = FOUND_NUMBER;
      }
      else
      {
        Serial.println("Couldn't find 211 Confirmation code is: text in reply, restart app!");
        status = CONNECT_NONE;
      }
      break;
    case FOUND_NUMBER:
      Serial.printf("Beacon %s has the secret number %d\r\n", name.c_str(), R);
      ESP_BT.flush();
      ESP_BT.disconnect();
      Serial.println("Disconnected from the beacon!");
      status = CONNECT_NONE;
      break;
    default:
      break;
  }
}
