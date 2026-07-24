/*
 *    Mars Rover - Arduino Code
 *    by Dejan, www.HowToMechatronics.com
 *    modified by Jack to add arm/claw support
 * 
 *   Libraries:
 *   ServoEasing: https://github.com/ArminJo/ServoEasing
 *   IBusBM: https://github.com/bmellink/IBusBM
 *   AccelStepper:http://www.airspayce.com/mikem/arduino/AccelStepper/index.html
 * Channel 0 which is 1 on the controller = Camera rotate
 * Channel 1 which is 2 on the controller = Camera tilt
 * Channel 2 which is 3 on the controller = Steering
 * channel 3 which is 4 on the controller = Speed
 * Channel 4 which is 5 on the controller = Direction
 * Channel 5 which is 6 on the controller = Claw actuate
 * Channel 6 which is 7 on the controller = Arm left/right
 * Channel 7 which is 8 on the controller = Arm Forward/Backward
 * Channel 8 which is 9 on the controller = Arm Up/Down            3 stage switch
 * Channel 9 which is 10 on the controller = Not used               Usage idea: Sirens so people know to get out of the rovers way
 */

#include <Arduino.h>
#include <AccelStepper.h>
#include <ServoEasing.hpp>
#include <IBusBM.h>
// #include <LiquidCrystal_I2C.h> //if we want to add a Liquid Crystal I2C display to the rover
#include <Servo.h>

#define motorW1_IN1 6
#define motorW1_IN2 7
#define motorW2_IN1 4
#define motorW2_IN2 5
#define motorW3_IN1 2
#define motorW3_IN2 3
#define motorW4_IN1 13
#define motorW4_IN2 10
#define motorW5_IN1 8
#define motorW5_IN2 9
#define motorW6_IN1 11
#define motorW6_IN2 12

ServoEasing servoW1;
ServoEasing servoW3;
ServoEasing servoW4;
ServoEasing servoW6;
ServoEasing servoCamTilt;

ServoEasing servoArmBaseSide;
ServoEasing servoArmBaseHeight;
ServoEasing servoArmMidHeight;
ServoEasing servoArmClawHeight;
ServoEasing servoArmClaw;

AccelStepper camPanStepper(1, 46, 45);  //(Type:driver, STEP, DIR) - Stepper1

IBusBM IBus;
IBusBM IBusSensor;

int angle = 0;   // servo position in degrees
int ch0, ch1, ch2, ch3, ch4, ch5, ch6, ch7, ch8, ch9;
int servo1Angle = 90;
int servo3Angle = 90;
int servo4Angle = 90;
int servo6Angle = 90;
int s = 0; // rover speed
int r = 0; // turning radius
int m1, m2, m3, m4, m5, m6;
int camTilt = 90;
int camPan = 0;
float speed1, speed2, speed3 = 0;
float speed1PWM, speed2PWM, speed3PWM = 0;
float thetaInnerFront, thetaInnerBack, thetaOuterFront, thetaOuterBack = 0;

float armForwardBackward = 0;
float armUpDown = 0;

int servoArmSideAngle = 90;
int servoArmBaseAngle = 90;
int servoArmMidAngle = 90;
int servoArmClawAngle = 90;
int servoClawAngle = 90;


float d1 = 271; // distance in mm
float d2 = 278;
float d3 = 301;
float d4 = 304;


void calculateMotorsSpeed() {
  // if no steering, all wheels speed is the same - straight move
  if (ch3 > 1485 && ch3 < 1515) {
    speed1 = speed2 = speed3 = s;
  }
  // when steering, wheels speed depend on the turning radius value
  else {
    // Outer wheels, furthest wheels from turning point, have max speed
    // Due to the rover geometry, all three outer wheels should rotate almost with the same speed. They differe only 1% so we asume they are the same.
    speed1 = s;
    // Inner front and back wheels are closer to the turing point and have lower speeds compared to the outer speeds
    speed2 = s * sqrt(pow(d3, 2) + pow((r - d1), 2)) / (r + d4);
    // Inner middle wheel is closest to the turning point, has the lowest speed
    speed3 = s * (r - d4) / (r + d4);
  }

  // speed value from 0 to 100% to PWM value from 0 to 255
  speed1PWM = map(round(speed1), 0, 100, 0, 255);
  speed2PWM = map(round(speed2), 0, 100, 0, 255);
  speed3PWM = map(round(speed3), 0, 100, 0, 255);
}

void calculateServoAngle() {
  // Calculate the angle for each servo for the input turning radius "r"
  thetaInnerFront = round((atan((d3 / (r + d1)))) * 180 / PI);
  thetaInnerBack = round((atan((d2 / (r + d1)))) * 180 / PI);
  thetaOuterFront = round((atan((d3 / (r - d1)))) * 180 / PI);
  thetaOuterBack = round((atan((d2 / (r - d1)))) * 180 / PI);

}

void arm() {
  //code for the arm/claw extension goes here
  servoArmBaseAngle = map(ch6, 1000, 2000, 0, 180);
  armForwardBackward = map(ch7, 1000, 2000, 0, 180);

  if(ch5 > 1500) {
    servoClawAngle = 0;
  } else if(ch5 < 1500) {
    servoClawAngle = 180;
  }

  if(ch8 > 1600) {
    armUpDown = 180;
  } else if(ch8 < 1400) {
    armUpDown = 0;
  } else if(ch8 == 1500) {
    armUpDown = 90;
  }
  
  servoArmBaseSide.startEaseTo(servoArmBaseAngle);
  servoArmBaseHeight.startEaseTo(servoArmBaseAngle);
  servoArmMidHeight.startEaseTo(servoArmMidAngle);
  servoArmClawHeight.startEaseTo(servoArmClawAngle);
  servoArmClaw.startEaseTo(servoClawAngle);
}

void serialSend() {
  if (Serial3.available() > 0) {
    int copy = Serial3.read();
    Serial.println("ESP-DATA:");
    Serial.println(copy);
  }
}

void setup() {
  /*
     Use this if you need to change the frequency of the PWM signals
    TCCR4B = TCCR4B & B11111000 | B00000101;     // D6,D7,D8 PWM frequency of 30.64 Hz
    TCCR2B = TCCR2B & B11111000 | B00000111;   // D9, D10 PWM frequency of 30.64 Hz
    TCCR1B = TCCR1B & B11111000 | B00000101;   // D11, D12  PWM frequency of 30.64 Hz
    TCCR5B = TCCR5B & B11111000 | B00000101; // D4, D13 PWM frequency of 30.64 Hz
    TCCR3B = TCCR3B & B11111000 | B00000101;    // D2, D3, D5 PWM frequency of 30.64 Hz
  */
  
  
  //We can connect the pi to the mega through usb and make them communicate over serial( USB)
  Serial.begin(115200);
  //And TX3 and RX3 can be connected to the ESP32 to make all devices send their data to the pi (or mega) 
  Serial3.begin(115200);
  IBus.begin(Serial1, IBUSBM_NOTIMER); // Servo iBUS
  IBusSensor.begin(Serial2, IBUSBM_NOTIMER); // Sensor iBUS

  IBusSensor.addSensor(IBUSS_INTV); // add voltage sensor

  servoW1.attach(22);
  servoW3.attach(23);
  servoW4.attach(24);
  servoW6.attach(25);
  servoCamTilt.attach(26);
  
  servoArmBaseSide.attach(27);
  servoArmBaseHeight.attach(28);
  servoArmMidHeight.attach(29);
  servoArmClawHeight.attach(30);
  servoArmClaw.attach(30);

  servoW1.write(90);
  servoW3.write(90);
  servoW4.write(90);
  servoW6.write(90);
  servoCamTilt.write(90);

  servoArmBaseSide.write(90);
  servoArmBaseHeight.write(90);
  servoArmMidHeight.write(90);
  servoArmClawHeight.write(90);
  servoArmClaw.write(90);

  servoW1.setSpeed(550);
  servoW3.setSpeed(550);
  servoW4.setSpeed(550);
  servoW6.setSpeed(550);
  servoCamTilt.setSpeed(200);

  servoArmBaseSide.setSpeed(200);
  servoArmBaseHeight.setSpeed(200);
  servoArmMidHeight.setSpeed(200);
  servoArmClawHeight.setSpeed(200);
  servoArmClaw.setSpeed(200);

  camPanStepper.setMaxSpeed(1000);
  camPan = 0;
  camTilt = 90;

  // DC Motors
  // Motor Wheel 1 - Left Front
  digitalWrite(motorW1_IN1, LOW);   // PWM value
  digitalWrite(motorW1_IN2, LOW); // Forward
  // Motor Wheel 2 - Left Middle
  digitalWrite(motorW2_IN1, LOW);
  digitalWrite(motorW2_IN2, LOW);
  // Motor Wheel 3 - Left Back
  digitalWrite(motorW3_IN1, LOW);
  digitalWrite(motorW3_IN2, LOW);
  // right side motors move in opposite direction
  // Motor Wheel 4 - Right Front
  digitalWrite(motorW4_IN1, LOW);
  digitalWrite(motorW4_IN2, LOW);
  // Motor Wheel 5 - Right Middle
  digitalWrite(motorW5_IN1, LOW);
  digitalWrite(motorW5_IN2, LOW);
  // Motor Wheel 6 - Right Back
  digitalWrite(motorW6_IN1, LOW);
  digitalWrite(motorW6_IN2, LOW);
}

void loop() {
  // Reading the data comming from the RC Transmitter
  IBus.loop();
  ch0 = IBus.readChannel(0);
  ch1 = IBus.readChannel(1);
  ch2 = IBus.readChannel(2);
  ch3 = IBus.readChannel(3);
  ch4 = IBus.readChannel(4);
  ch5 = IBus.readChannel(5);
  ch6 = IBus.readChannel(6);
  ch7 = IBus.readChannel(7);
  ch8 = IBus.readChannel(8);
  ch9 = IBus.readChannel(9);

  // Convertign the incoming data
  // Steering right
  if (ch3 > 1515) {
    r = map(ch3, 1515, 2000, 1400, 600); // turning radius from 1400mm to 600mm
    //Serial.print("steer right value on channel 3 = ");
   // Serial.println(ch3);
  }
  // Steering left
  else if (ch3 < 1485) {
    r = map(ch3, 1485, 1000, 1400, 600); // turning radius from 600mm to 1400mm
   // Serial.print("steer left value on channel 03= ");
    //Serial.println(ch3);
  }
  // Rover speed in % from 0 to 100
  s = map(ch2, 1000, 2000, 0, 100); // rover speed from 0% to 100%

  // Camera head steering
  if (ch1 < 1485 ) {
    if (camTilt >= 35) {
      camTilt--;
      delay(20);
    }
  }
  if (ch1 > 1515 ) {
    if (camTilt <= 165) {
      camTilt++;
      delay(20);
    }
  }
  servoCamTilt.startEaseTo(camTilt); // Camera tilt

  if (ch0 >= 1000 && ch0 < 1485) {
    camPan = map(ch0, 1000, 1485, 400, 0);
  }
  else if (ch0 > 1515 && ch0 <= 2000) {
    camPan = map(ch0, 1515, 2000, 0, -400);
  }
  else {
    camPan = 0;
  }
  camPanStepper.setSpeed(camPan);    // Camera pan
  camPanStepper.run();


  calculateMotorsSpeed();
  calculateServoAngle();

  // Steer right
  if (ch3 > 1515) {
    // Servo motors
    // Outer wheels
    servoW1.startEaseTo(97 + thetaInnerFront); // front wheel steer right
    servoW3.startEaseTo(97 - thetaInnerBack); // back wheel steer left for overall steering to the right of the rover
    // Inner wheels
    servoW4.startEaseTo(94 + thetaOuterFront);
    servoW6.startEaseTo(96 - thetaOuterBack);

    // DC Motors
    if (ch4 > 1500) { // Move forward
      // Motor Wheel 1 - Left Front
      analogWrite(motorW1_IN1, speed1PWM);   // Outer wheels running at speed1 - max speed
      digitalWrite(motorW1_IN2, LOW);
      // Motor Wheel 2 - Left Middle
      analogWrite(motorW2_IN1, speed1PWM);
      digitalWrite(motorW2_IN2, LOW);
      // Motor Wheel 3 - Left Back
      analogWrite(motorW3_IN1, speed1PWM); 
      digitalWrite(motorW3_IN2, LOW);
      // right side motors move in opposite direction
      // Motor Wheel 4 - Right Front
      digitalWrite(motorW4_IN1, LOW);
      analogWrite(motorW4_IN2, speed2PWM); // Inner front wheel running at speed2 - lower speed
      // Motor Wheel 5 - Right Middle
      digitalWrite(motorW5_IN1, LOW);
      analogWrite(motorW5_IN2, speed3PWM); // Inner middle wheel running at speed3 - lowest speed
      // Motor Wheel 6 - Right Back
      digitalWrite(motorW6_IN1, LOW);
      analogWrite(motorW6_IN2, speed2PWM); // Inner back wheel running at speed2 - lower speed
    }
    else if (ch4 < 1500) {
      // Motor Wheel 1 - Left Front
      digitalWrite(motorW1_IN1, LOW);   // Outer wheels running at speed1 - max speed
      analogWrite(motorW1_IN2, speed1PWM);
      // Motor Wheel 2 - Left Middle
      digitalWrite(motorW2_IN1, LOW);
      analogWrite(motorW2_IN2, speed1PWM);
      // Motor Wheel 3 - Left Back
      digitalWrite(motorW3_IN1, LOW);
      analogWrite(motorW3_IN2, speed1PWM);   
           // right side motors move in opposite direction
      // Motor Wheel 4 - Right Front
      analogWrite(motorW4_IN1, speed2PWM);
      digitalWrite(motorW4_IN2, LOW); // Inner front wheel running at speed2 - lower speed
      // Motor Wheel 5 - Right Middle
      analogWrite(motorW5_IN1, speed3PWM);
      digitalWrite(motorW5_IN2, LOW); // Inner middle wheel running at speed3 - lowest speed
      // Motor Wheel 6 - Right Back
      analogWrite(motorW6_IN1, speed2PWM);
      digitalWrite(motorW6_IN2, LOW); // Inner back wheel running at speed2 - lower speed
    }
  }

  // Steer left
  else if (ch3 < 1485) {
    // Servo motors
    servoW1.startEaseTo(97 - thetaOuterFront);
    servoW3.startEaseTo(97 + thetaOuterBack);
    servoW4.startEaseTo(94 - thetaInnerFront);
    servoW6.startEaseTo(96 + thetaInnerBack);

    // DC Motors
    if (ch4 > 1500) { // Move forward
      // Motor Wheel 1 - Left Front
      analogWrite(motorW1_IN1, speed2PWM);   // PWM value
      digitalWrite(motorW1_IN2, LOW); // Forward
      // Motor Wheel 2 - Left Middle
      analogWrite(motorW2_IN1, speed3PWM);
      digitalWrite(motorW2_IN2, LOW);
      // Motor Wheel 3 - Left Back
      analogWrite(motorW3_IN1, speed2PWM);
      digitalWrite(motorW3_IN2, LOW);
      // Motor Wheel 4 - Right Front
      // right side motors move in opposite direction
      digitalWrite(motorW4_IN1, LOW);
      analogWrite(motorW4_IN2, speed1PWM);
      // Motor Wheel 5 - Right Middle
      digitalWrite(motorW5_IN1, LOW);
      analogWrite(motorW5_IN2, speed1PWM);
      // Motor Wheel 6 - Right Back
      digitalWrite(motorW6_IN1, LOW);
      analogWrite(motorW6_IN2, speed1PWM);
    }
    else if (ch4 < 1500) { // Move backward
      // Motor Wheel 1 - Left Front
      digitalWrite(motorW1_IN1, LOW);   // PWM value
      analogWrite(motorW1_IN2, speed2PWM); // Forward
      // Motor Wheel 2 - Left Middle
      digitalWrite(motorW2_IN1, LOW);
      analogWrite(motorW2_IN2, speed3PWM);
      // Motor Wheel 3 - Left Back
      digitalWrite(motorW3_IN1, LOW);
      analogWrite(motorW3_IN2, speed2PWM);
      // Motor Wheel 4 - Right Front
      // right side motors move in opposite direction
      analogWrite(motorW4_IN1, speed1PWM);
      digitalWrite(motorW4_IN2, LOW);
      // Motor Wheel 5 - Right Middle
      analogWrite(motorW5_IN1, speed1PWM);
      digitalWrite(motorW5_IN2, LOW);
      // Motor Wheel 6 - Right Back
      analogWrite(motorW6_IN1, speed1PWM);
      digitalWrite(motorW6_IN2, LOW);
    }
  }

  // Move straight
  else {
    servoW1.startEaseTo(97);
    servoW3.startEaseTo(97);
    servoW4.startEaseTo(94);
    servoW6.startEaseTo(96);

    // DC Motors
    if (ch4 > 1500) {
      // Motor Wheel 1 - Left Front
      analogWrite(motorW1_IN1, speed1PWM);  // all wheels move at the same speed
      digitalWrite(motorW1_IN2, LOW); // Forward
      // Motor Wheel 2 - Left Middle
      analogWrite(motorW2_IN1, speed1PWM);
      digitalWrite(motorW2_IN2, LOW);
      // Motor Wheel 3 - Left Back
      analogWrite(motorW3_IN1, speed1PWM);
      digitalWrite(motorW3_IN2, LOW);
      // right side motors move in opposite direction
      // Motor Wheel 4 - Right Front
       digitalWrite(motorW4_IN1, LOW);
       analogWrite(motorW4_IN2, speed1PWM);
           // Motor Wheel 5 - Right Middle
      digitalWrite(motorW5_IN1, LOW);
       analogWrite(motorW5_IN2, speed1PWM);
                 // Motor Wheel 6 - Right Back
      digitalWrite(motorW6_IN1, LOW);
      analogWrite(motorW6_IN2, speed1PWM);
          }
    else if (ch4 < 1500) {
      // Motor Wheel 1 - Left Front
      digitalWrite(motorW1_IN1, LOW);  // all wheels move at the same speed
      analogWrite(motorW1_IN2, speed1PWM); // Forward
      // Motor Wheel 2 - Left Middle
      digitalWrite(motorW2_IN1, LOW);
      analogWrite(motorW2_IN2, speed1PWM);
      // Motor Wheel 3 - Left Back
      digitalWrite(motorW3_IN1, LOW);
      analogWrite(motorW3_IN2, speed1PWM);
      // right side motors move in opposite direction
      // Motor Wheel 4 - Right Front
      analogWrite(motorW4_IN1, speed1PWM);
      digitalWrite(motorW4_IN2, LOW);
                  // Motor Wheel 5 - Right Middle
       analogWrite(motorW5_IN1, speed1PWM);
       digitalWrite(motorW5_IN2, LOW);
           // Motor Wheel 6 - Right Back
            analogWrite(motorW6_IN1, speed1PWM);
            digitalWrite(motorW6_IN2, LOW);
    }
  }


  Serial.println("Channel 5: ");
  Serial.println(ch5);
  Serial.println("Channel 6: ");
  Serial.println(ch6);
  Serial.println("Channel 7: ");
  Serial.println(ch7);
  Serial.println("Channel 8: ");
  Serial.println(ch8);
  Serial.println("Channel 9: ");
  Serial.println(ch9);
  // arm();

  serialSend();

  // Monitor the battery voltage
  int sensorValue = analogRead(A0);
  float voltage = sensorValue * (5.00 / 1023.00) * 3.02; // Convert the reading values from 5v to suitable 12V
  // Send battery voltage value to transmitter
  IBusSensor.loop();
  IBusSensor.setSensorMeasurement(1, voltage * 100);
}