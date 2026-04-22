#include <Arduino.h>
#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(2000); // Đợi mạch khởi động ổn định

  Serial.println("\n------------------------------------");
  // Bật Wi-Fi ở chế độ Station để truy xuất địa chỉ MAC vật lý
  WiFi.mode(WIFI_STA); 
  
  Serial.print("ĐỊA CHỈ MAC CỦA BOARD NÀY: ");
  Serial.println(WiFi.macAddress());
  Serial.println("------------------------------------\n");
}

void loop() {
  // Không làm gì cả
}