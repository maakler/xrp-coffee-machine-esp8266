#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

#include <WebSocketsClient.h>

#include <ArduinoJson.h>

#include <Hash.h>


// Declare an instance of the ESP8266WiFiMulti class, which can store and manage multiple WiFi connections
ESP8266WiFiMulti WiFiMulti;
WebSocketsClient webSocket;


// State machine
unsigned long currentTime;
unsigned long previousTime1;
unsigned long previousTime2;
unsigned long interval1 = 5000; // run function1 every 1 second
unsigned long interval2 = 20000; // run function2 every 5 seconds
int state = 0;
int seconds = 0;


// Set up a macro to use for printing debug messages to the serial port
#define USE_SERIAL Serial

void countXRPDifference(JsonArray affected_nodes, String address) {
  for (unsigned int i = 0; i < sizeof(affected_nodes); i++) {
    if (affected_nodes[i].containsKey("ModifiedNode")) {
      JsonObject ledger_entry = affected_nodes[i]["ModifiedNode"];
      if (ledger_entry["LedgerEntryType"] == "AccountRoot" &&
          ledger_entry["FinalFields"]["Account"] == address) {
        if (!ledger_entry["PreviousFields"].containsKey("Balance")) {
          USE_SERIAL.println("XRP balance did not change.");
        }
        double old_balance(ledger_entry["PreviousFields"]["Balance"]);
        double new_balance(ledger_entry["FinalFields"]["Balance"]);
        double diff_in_drops = new_balance - old_balance;
        double xrp_amount = diff_in_drops / 1e6;
        if (xrp_amount >= 0) {
          USE_SERIAL.print("Received ");
          USE_SERIAL.print(xrp_amount);
          USE_SERIAL.println(" XRP.");
          return;
        } else {
          USE_SERIAL.print("Spent ");
          USE_SERIAL.print(xrp_amount);
          USE_SERIAL.println(" XRP.");
          return;
        }
      }
    } else if (affected_nodes[i].containsKey("CreatedNode")) {
      JsonObject ledger_entry = affected_nodes[i]["CreatedNode"];
      if (ledger_entry["LedgerEntryType"] == "AccountRoot" &&
          ledger_entry["NewFields"]["Account"] == address) {
        double balance_drops(ledger_entry["NewFields"]["Balance"]);
        double xrp_amount = balance_drops / 1e6;
        USE_SERIAL.print("Received ");
        USE_SERIAL.print(xrp_amount);
        USE_SERIAL.println(" XRP (account funded).");
        return;
      }
    }
  }
  USE_SERIAL.println("Did not find address in affected nodes.");
}

void countXRPReceived(JsonObject tx, String address) {
	if (tx["meta"]["TransactionResult"] != "tesSUCCESS") {
		USE_SERIAL.println("Transaction failed.");
		return;
	}
	if (tx["transaction"]["TransactionType"] == "Payment") {
		if (tx["transaction"]["Destination"] != address) {
			USE_SERIAL.println("Not the destination of this payment.");
			return;
		}
		if (tx["meta"]["delivered_amount"]) {
			double amount_in_drops(tx["meta"]["delivered_amount"]);
			double xrp_amount = amount_in_drops / 1e6;
			USE_SERIAL.print("Received ");
			USE_SERIAL.print(xrp_amount);
			USE_SERIAL.println(" XRP.");
			return;
		} else {
			USE_SERIAL.println("Received non-XRP currency.");
			return;
		}
	} else if (tx["transaction"]["TransactionType"] == "PaymentChannelClaim" ||
			tx["transaction"]["TransactionType"] == "PaymentChannelFund" ||
			tx["transaction"]["TransactionType"] == "OfferCreate" ||
			tx["transaction"]["TransactionType"] == "CheckCash" ||
			tx["transaction"]["TransactionType"] == "EscrowFinish") {
		countXRPDifference(tx["meta"]["AffectedNodes"], address);
	} else {
		USE_SERIAL.print("Not a currency-delivering transaction type (");
		String type = tx["transaction"]["TransactionType"];
		USE_SERIAL.print(type);
		USE_SERIAL.println(").");
	}
}

// Use this to send API requests
void apiRequest(DynamicJsonDocument options) {
  String output;
  serializeJson(options, output);
  webSocket.sendTXT(output);
}

// Tests functionality of API_Requst
void pingpong() {
  DynamicJsonDocument command(1024);
  command["id"] = "on_open_ping_1";
  command["command"] = "ping";
  apiRequest(command);
}

void subscribe(String account) {
  DynamicJsonDocument command(1024);
  command["command"] = "subscribe";
  JsonArray accounts = command.createNestedArray("accounts");
  accounts.add(account);
  apiRequest(command);
}

// Declare a function to handle events from the WebSocketsClient instance
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {

	// Switch statement to handle different event types
	switch(type) {

		case WStype_DISCONNECTED:
 			// Print a message when the WebSocket client is disconnected
			USE_SERIAL.printf("[WSc] Disconnected!\n");
			break;

		case WStype_CONNECTED: {
      // Print a message and send a message to the server when the WebSocket client is connected
			USE_SERIAL.printf("[WSc] Connected to url: %s\n", payload);
			pingpong();
			subscribe("r9Cy4BWuND8goFkoupSNpZtyLiN9UeZS1t");
		}
			break;

		case WStype_BIN:
      // Print a message and print the data when the WebSocket client receives binary data
			// USE_SERIAL.printf("[WSc] get binary length: %u\n", length);
			// hexdump(payload, length);

			// send data to server
			// webSocket.sendBIN(payload, length);
			break;

        case WStype_PING:
            // Print a message when the WebSocket client receives a ping
            // pong will be send automatically
            // USE_SERIAL.printf("[WSc] get ping\n");
            break;
            
        case WStype_PONG:
            // Print a message when the WebSocket client receives a pong
            // answer to a ping we send
            // USE_SERIAL.printf("[WSc] get pong\n");
            break;

		case WStype_TEXT:
      // Print a message when the WebSocket client receives text data
			// try to decipher the JSON string received
			StaticJsonDocument<2000> doc;                    // create a JSON container
			DeserializationError error = deserializeJson(doc, payload);
			if (error) {
				Serial.print(F("deserializeJson() failed: "));
				Serial.println(error.f_str());
				return;
			} 
			else {
				JsonObject obj = doc.as<JsonObject>();
				countXRPReceived(obj, "r9Cy4BWuND8goFkoupSNpZtyLiN9UeZS1t");
			}
			// USE_SERIAL.printf("[WSc] get text: %s\n", payload);
			break;
    }

}

void setup() {
	// Set up the serial port
	USE_SERIAL.begin(115200);

	//Serial.setDebugOutput(true);
	// USE_SERIAL.setDebugOutput(true);

	USE_SERIAL.println();
	USE_SERIAL.println();
	USE_SERIAL.println();

  // Print a countdown message before booting the device
	for(uint8_t t = 4; t > 0; t--) {
		USE_SERIAL.printf("[SETUP] BOOT WAIT %d...\n", t);
		USE_SERIAL.flush();
		delay(1000);
	}

  // Add a WiFi access point to the WiFiMulti instance
	WiFiMulti.addAP("Elion-651052", "RMNX3Y7FPD");

	// Try to connect to a WiFi access point using the WiFiMulti instance
	while(WiFiMulti.run() != WL_CONNECTED) {
		delay(100);
    	Serial.print(".");
	}
	USE_SERIAL.println("");
	USE_SERIAL.println("WiFi connected");  
	USE_SERIAL.println("IP address: ");
	USE_SERIAL.println(WiFi.localIP());


	// Set up the WebSocket client to connect to a WebSocket server at a specific address, port, and URL
	webSocket.begin("s.altnet.rippletest.net", 51233, "wss://s.altnet.rippletest.net:51233");

	// Set the event handler function for the WebSocket client
	webSocket.onEvent(webSocketEvent);

	// Set the reconnect interval for the WebSocket client (optional)
	webSocket.setReconnectInterval(5000);
  
	// Enable the heartbeat feature for the WebSocket client (optional)
	// The client will send a ping to the server every 15000 ms, and expect a pong within 3000 ms
	// If the client doesn't receive a pong 2 times, it will consider the connection disconnected
	// webSocket.enableHeartbeat(15000, 3000, 2);
	
	// State machine
	// set the initial previous time to the current time
	previousTime1 = millis();
	previousTime2 = millis();
}


// 5 sec
void function1() {
  // code for function 1
  
	state = 1;
}


// 20 sec
void function2() {
  // code for function 2
  state = 0;

}


void loop() {
	currentTime = millis();

	// Check for websocket updates
	webSocket.loop();

	// State machine
	switch (state) {
		case 0:
			function1();
			if (currentTime - previousTime1 >= interval1) {
				previousTime1 = currentTime;
				
				
				Serial.println("...");
				seconds++;
			}
			break;
		case 1:
			function2();
			if (currentTime - previousTime2 >= interval2) {
				previousTime2 = currentTime;
				Serial.print(seconds);
				Serial.println(" seconds passed");
			}
			break;
	}
}
