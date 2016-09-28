#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <sys/fcntl.h>

#include <wiringPi.h>
#include <wiringShift.h>

#define del 55

unsigned long lastMillisP = 0;
unsigned long lastMillis = 0;

/*
 +------+-----+----------+------ Model ODROID-XU3/4 ------+----------+-----+------+
 | GPIO | wPi |   Name   | Mode | V | Physical | V | Mode |   Name   | wPi | GPIO |
 +------+-----+----------+------+---+----++----+---+------+----------+-----+------+
 |      |     |     3.3v |      |   |  1 || 2  |   |      | 5v       |     |      |
 |      |     | I2C1.SDA | ALT5 | 1 |  3 || 4  |   |      | 5V       |     |      |
 |      |     | I2C1.SCL | ALT5 | 1 |  5 || 6  |   |      | 0v       |     |      |
 |   18 |   7 | GPIO. 18 |   IN | 1 |  7 || 8  | 1 | ALT5 | UART0.TX |     |      |
 |      |     |       0v |      |   |  9 || 10 | 1 | ALT5 | UART0.RX |     |      |
 |  174 |   0 | GPIO.174 |  OUT | 0 | 11 || 12 | 0 | OUT  | GPIO.173 | 1   |  173 |
 |   21 |   2 | GPIO. 21 |  OUT | 1 | 13 || 14 |   |      | 0v       |     |      |
 |   22 |   3 | GPIO. 22 |  OUT | 0 | 15 || 16 | 0 | OUT  | GPIO. 19 | 4   |  19  |
 |      |     |     3.3v |      |   | 17 || 18 | 0 | OUT  | GPIO. 23 | 5   |  23  |
 |      |     |     MOSI | ALT5 | 1 | 19 || 20 |   |      | 0v       |     |      |
 |      |     |     MISO | ALT5 | 1 | 21 || 22 | 1 | IN   | GPIO. 24 | 6   |  24  |
 |      |     |       0v |      |   | 25 || 26 | 1 | OUT  | GPIO. 25 |     |      |
 |   28 |  21 | GPIO. 28 |   IN | 1 | 29 || 30 |   |      | 0v       |     |      |
 |   30 |  22 | GPIO. 30 |   IN | 1 | 31 || 32 | 1 | IN   | GPIO. 29 |     |      |
 |   31 |  23 | GPIO. 31 |   IN | 1 | 33 || 34 |   |      | 0v       |     |      |
 |      |     |       0v |      |   | 39 || 40 |   |      | AIN.3    |     |      |
 +------+-----+----------+------+---+----++----+---+------+----------+-----+------+

   shift register pins:
        +--------+
  2out -+ 1   16 +- Vin
  3out -+ 2   15 +- 1out
  4out -+ 3   14 +- data (blue)
  5out -+ 4   13 +- ground
  6out -+ 5   12 +- latch (green + 1u cap if first)
  7out -+ 6   11 +- lock (yellow)
  8out -+ 7   10 +- Vin
   gnd -+ 8    9 +- serial out
        +--------+
*/
const int dataPin  = 1; // blue (pin 14)
const int latchPin = 4; // green (pin 12)
const int clockPin = 5; // yellow (pin 11)

const int inputPin = 6;

static const char dev_name[100] = "/sys/devices/virtual/thermal/thermal_zone0/temp";

static const int cols[3] = { 0, 2, 3 };

const char digit[10] = {
  //ABCDEFG.
  //ABGC.DEF
  0b11110111, // 0
  0b01010000, // 1
  0b11100111, // 2
  0b11110100, // 3
  0b01110101, // 4
  0b10110101, // 5
  0b10110111, // 6
  0b11010001, // 7
  0b11110111, // 8
  0b11110101  // 9
};

void usage(char *a);
static void die(int sig);
//void pd(int c, int d, int f);
void pickDigit(int x);
void printDigit(int col, int number, int dp);
void clearLEDs();

int main(int argc, char **argv)
{
  int integer, decimal;
  int fd = -1, ret;
  char *buf, tmp[10];
  long value = 0;
  char buffer[100];
  char errmsg[100];
  int i;
  int num = 0;
  int tens, ones, decpA, decpB, tmp_int;
  float temp;
  unsigned long curMillis;
  int lastInt = 0;
  int flag = 1;

  // note: we're assuming BSD-style reliable signals here
  (void)signal(SIGINT, die);
  (void)signal(SIGHUP, die);
  if (wiringPiSetup () == -1)
  {
    (void)fprintf(stderr, "oops %d\n", errno);
    return 1;
  }
  //piHiPri(20);
  for (i = 0; i < 3; i++)
  {
    pinMode(cols[i], OUTPUT);
    digitalWrite(cols[i], HIGH);
  }
  pinMode(inputPin, INPUT);
  pullUpDnControl(inputPin, PUD_UP);
  pinMode(latchPin, OUTPUT);
  pinMode(dataPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  /*
  for (i = 0; i < 10; i++)
  {
    printDigit(2, i, 1);
    delay(250);
  }
  */
  while (1)
  {
    curMillis = millis();
    if (curMillis - lastMillis >= 4000)
    {
      lastMillis = curMillis;
      if ((fd = open(dev_name, O_RDONLY)) < 0)
      {
        sprintf(errmsg, "Error opening device: %s", dev_name);
        perror(errmsg);
        exit(1);
      }
      ret = read(fd, buffer, sizeof(buffer));
      if (ret < 0)
      {
        perror("read error");
        exit(1);
      }
      value = atoi(buffer);
    }
    integer = value / 1000;
    decimal = value % 1000;
    tens = (int)integer / 10;
    ones = (int)integer % 10;
    tmp_int = decimal * 10;
    decpA = (int)tmp_int % 10;
    if (digitalRead(inputPin) == LOW && tens != 0)
    {
      flag = 0;
    }
    while (flag == 0)
    {
      curMillis = millis();

      /*
      clearLEDs();
      printDigit(2, decpA, 0);
      delayMicroseconds(del);
      */

      //printf("%d%d\n", tens, ones);
      clearLEDs();
      printDigit(2, decpA, 0);
      delayMicroseconds(del);

      clearLEDs();
      printDigit(1, ones, 1);
      delayMicroseconds(del);

      clearLEDs();
      printDigit(0, tens, 0);
      delayMicroseconds(del);

      if (curMillis - lastMillisP >= 5000)
      {
        lastMillisP = curMillis;
        flag = 1;
      }
    }
    clearLEDs();
    close(fd);
  }
  return 0;
}

void usage(char *a)
{
  printf("%s [num]\n", a);
}

static void die(int sig)
{
  int i;
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, LSBFIRST, 0b00000000);
  digitalWrite(latchPin, HIGH);
  if (sig != 0 && sig != 2)
      (void)fprintf(stderr, "caught signal %d\n", sig);
  if (sig == 2)
      (void)fprintf(stderr, "Exiting due to Ctrl + C\n");
  exit(0);
}

/*
void pd(int c, int d, int f)
{
    clearLEDs();
    pickDigit(c);
    printDigit(d, f);
    delayMicroseconds(del);
}
*/

void pickDigit(int x)
{
  for (int i = 0; i < 3; i++)
  {
    if (i != x)
      digitalWrite(cols[i], HIGH);
  }
  digitalWrite(cols[x], LOW);
}

void printDigit(int col, int number, int dp)
{
  char c;
  c = digit[number];
  if (dp == 1)
    c |= 1 << 4;
  else
    c &= ~(1 << 4);
  switch (col)
  {
    case 0:
  digitalWrite(cols[0], LOW);
  digitalWrite(cols[1], HIGH);
  digitalWrite(cols[2], HIGH);
  break;
    case 1:
  digitalWrite(cols[1], LOW);
  digitalWrite(cols[0], HIGH);
  digitalWrite(cols[2], HIGH);
  break;
    case 2:
  digitalWrite(cols[2], LOW);
  digitalWrite(cols[0], HIGH);
  digitalWrite(cols[1], HIGH);
  break;
  }
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, LSBFIRST, c);
  digitalWrite(latchPin, HIGH);
}

void clearLEDs()
{
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, LSBFIRST, 0b00000000);
  digitalWrite(latchPin, HIGH);
}

