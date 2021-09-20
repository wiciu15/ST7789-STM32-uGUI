#include "st7789.h"
#include "stdio.h"
#include "stdint.h"

UG_GUI gui;
static int8_t currentSPISize=-1;
#define mode_16bit    1
#define mode_8bit     0
/*
 * @brief Sets SPI interface word size (0=8bit, 1=16 bit)
 * @param none
 * @return none
 */
static void setSPI_Size(int8_t size){
  if(currentSPISize!=size){
    currentSPISize=size;
    if(size==mode_16bit){
      ST7789_SPI_PORT.Init.DataSize = SPI_DATASIZE_16BIT;
    }
    else{
      ST7789_SPI_PORT.Init.DataSize = SPI_DATASIZE_8BIT;
    }
    HAL_SPI_Init(&ST7789_SPI_PORT);
  }
}

#ifdef USE_DMA
#define DMA_min_Sz    16
#define mem_increase  1
#define mem_fixed     0

static int8_t currentDMASize=-1;
static int8_t currentMemInc=-1;
/**
 * @brief Configures DMA/ SPI interface
 * @param memInc Enable/disable memory address increase
 * @param mode16 Enable/disable 16 bit mode (disabled = 8 bit)
 * @return none
 */
static void setDMAMemMode(uint8_t memInc, uint8_t size){

  setSPI_Size(size);
  if(currentDMASize!=size || currentMemInc!=memInc){
    currentDMASize=size;
    currentMemInc=memInc;
    __HAL_DMA_DISABLE(ST7789_SPI_PORT.hdmatx);
    if(memInc==mem_increase){
      ST7789_SPI_PORT.hdmatx->Init.MemInc = DMA_MINC_ENABLE;
    }
    else{
      ST7789_SPI_PORT.hdmatx->Init.MemInc = DMA_MINC_DISABLE;
    }
    if(size==mode_16bit){
      ST7789_SPI_PORT.hdmatx->Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
      ST7789_SPI_PORT.hdmatx->Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    }
    else{
      ST7789_SPI_PORT.hdmatx->Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
      ST7789_SPI_PORT.hdmatx->Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    }
    HAL_DMA_Init(ST7789_SPI_PORT.hdmatx);
  }
}
#endif

/**
 * @brief Write command to ST7789 controller
 * @param cmd -> command to write
 * @return none
 */
static void ST7789_WriteCommand(uint8_t cmd)
{
  setSPI_Size(mode_8bit);
  ST7789_Select();
  ST7789_DC_Clr();
  HAL_SPI_Transmit(&ST7789_SPI_PORT, &cmd, sizeof(cmd), HAL_MAX_DELAY);
  ST7789_UnSelect();
}

/**
 * @brief Write data to ST7789 controller
 * @param buff -> pointer of data buffer
 * @param buff_size -> size of the data buffer
 * @return none
 */
static void ST7789_WriteData(uint8_t *buff, size_t buff_size)
{
  ST7789_Select();
  ST7789_DC_Set();

  // split data in small chunks because HAL can't send more than 64K at once

  while (buff_size > 0) {
    uint16_t chunk_size = buff_size > 65535 ? 65535 : buff_size;
    #ifdef USE_DMA
    if(DMA_min_Sz<=buff_size){
      HAL_SPI_Transmit_DMA(&ST7789_SPI_PORT, buff, chunk_size);
      while(ST7789_SPI_PORT.hdmatx->State!=HAL_DMA_STATE_READY){
        asm("nop");                                              // Fix for current STM32F1 libraries, HAL_DMA_StateTypeDef is not being declared as volatile so optimizations will break this check
      }
    }
    else{
      HAL_SPI_Transmit(&ST7789_SPI_PORT, buff, chunk_size, HAL_MAX_DELAY);
    }
    #else
    HAL_SPI_Transmit(&ST7789_SPI_PORT, buff, chunk_size, HAL_MAX_DELAY);
    #endif
    buff += chunk_size;
    buff_size -= chunk_size;
  }

  ST7789_UnSelect();
}
/**
 * @brief Write data to ST7789 controller, simplify for 8bit data.
 * data -> data to write
 * @return none
 */
static void ST7789_WriteSmallData(uint8_t data)
{
  setSPI_Size(mode_8bit);
  ST7789_Select();
  ST7789_DC_Set();
  HAL_SPI_Transmit(&ST7789_SPI_PORT, &data, sizeof(data), HAL_MAX_DELAY);
  ST7789_UnSelect();
}

/**
 * @brief Set the rotation direction of the display
 * @param m -> rotation parameter(please refer it in st7789.h)
 * @return none
 */
void ST7789_SetRotation(uint8_t m)
{
  ST7789_WriteCommand(ST7789_MADCTL); // MADCTL
  switch (m) {
  case 0:
    ST7789_WriteSmallData(ST7789_MADCTL_MX | ST7789_MADCTL_MY | ST7789_MADCTL_RGB);
    break;
  case 1:
    ST7789_WriteSmallData(ST7789_MADCTL_MY | ST7789_MADCTL_MV | ST7789_MADCTL_RGB);
    break;
  case 2:
    ST7789_WriteSmallData(ST7789_MADCTL_RGB);
    break;
  case 3:
    ST7789_WriteSmallData(ST7789_MADCTL_MX | ST7789_MADCTL_MV | ST7789_MADCTL_RGB);
    break;
  default:
    break;
  }
}


/**
 * @brief Set address of DisplayWindow
 * @param xi&yi -> coordinates of window
 * @return none
 */
static void ST7789_SetAddressWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
  int16_t x_start = x0 + X_SHIFT, x_end = x1 + X_SHIFT;
  int16_t y_start = y0 + Y_SHIFT, y_end = y1 + Y_SHIFT;

  /* Column Address set */
  ST7789_WriteCommand(ST7789_CASET);
  {
    uint8_t data[] = {x_start >> 8, x_start & 0xFF, x_end >> 8, x_end & 0xFF};
    ST7789_WriteData(data, sizeof(data));
  }

  /* Row Address set */
  ST7789_WriteCommand(ST7789_RASET);
  {
    uint8_t data[] = {y_start >> 8, y_start & 0xFF, y_end >> 8, y_end & 0xFF};
    ST7789_WriteData(data, sizeof(data));
  }
  /* Write to RAM */
  ST7789_WriteCommand(ST7789_RAMWR);
}

/**
 * @brief Draw a raw Pixel, wherever the cursor is at.
 * @param x&y -> coordinate to Draw
 * @param color -> color of the Pixel
 * @return none
 */
void ST7789_DrawRawPixel(uint16_t color)
{
  //uint8_t data[2] = {color >> 8, color & 0xFF};
  ST7789_Select();
  HAL_SPI_Transmit(&ST7789_SPI_PORT, (uint8_t*)&color, 1, HAL_MAX_DELAY);
  ST7789_UnSelect();
}

/**
 * @brief Set address of DisplayWindow and returns raw pixel draw for uGUI driver acceleration
 * @param xi&yi -> coordinates of window
 * @return none
 */

void(*ST7789_FillArea(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1))(uint16_t){
  ST7789_SetAddressWindow(x0,y0,x1,y1);
  setSPI_Size(mode_16bit);                                                          // Set SPI to 16 bit
  ST7789_DC_Set();
  return ST7789_DrawRawPixel;
}


/**
 * @brief Address and draw a Pixel
 * @param x&y -> coordinate to Draw
 * @param color -> color of the Pixel
 * @return none
 */
void ST7789_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  if ((x < 0) || (x >= ST7789_WIDTH) ||
     (y < 0) || (y >= ST7789_HEIGHT)) return;

  uint8_t data[2] = {color >> 8, color & 0xFF};

  ST7789_SetAddressWindow(x, y, x, y);

  ST7789_DC_Set();
  ST7789_Select();

  HAL_SPI_Transmit(&ST7789_SPI_PORT, data, sizeof(data), HAL_MAX_DELAY);
  ST7789_UnSelect();
}

void ST7789_FillPixels(uint16_t pixels, uint16_t color){
#ifdef USE_DMA
  if(DMA_min_Sz<=pixels){
    setDMAMemMode(mem_fixed, mode_16bit);                                             // Set SPI and DMA to 16 bit, enable memory increase
    ST7789_WriteData((uint8_t*)&color, pixels);
  }
  else{
  #endif
    setSPI_Size(mode_16bit);                                                          // Set SPI to 16 bit
    uint16_t fill[64];                                                                // Use a 64 pixel (128Byte) buffer for faster filling
    uint8_t blockSz;

    if(pixels<64){                                                                    // Adjust block size
      blockSz=pixels;
    }
    else{
      blockSz=64;
    }
    for(uint8_t t=0;t<blockSz;t++){                                                   // Fill the buffer with the color
      fill[t]=color;
    }
    while(pixels>=blockSz){                                                           // Send 64 pixel blocks
      ST7789_WriteData((uint8_t*)&fill, blockSz);
      pixels-=blockSz;
    }
    if(pixels){                                                                       // Send remaining pixels
      ST7789_WriteData((uint8_t*)&fill, pixels);
    }
  #ifdef USE_DMA
  }
  #endif
}
/**
 * @brief Fill an Area with single color
 * @param xSta&ySta -> coordinate of the start point
 * @param xEnd&yEnd -> coordinate of the end point
 * @param color -> color to Fill with
 * @return none
 */
int8_t ST7789_Fill(uint16_t xSta, uint16_t ySta, uint16_t xEnd, uint16_t yEnd, uint16_t color)
{
  uint16_t pixels = (xEnd-xSta+1)*(yEnd-ySta+1);
  ST7789_SetAddressWindow(xSta, ySta, xEnd, yEnd);
  ST7789_FillPixels(pixels, color);
  return UG_RESULT_OK;
}


/**
 * @brief Draw an Image on the screen
 * @param x&y -> start point of the Image
 * @param w&h -> width & height of the Image to Draw
 * @param data -> pointer of the Image array
 * @return none
 */
void ST7789_DrawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, void* data)
{
  if ((x >= ST7789_WIDTH) || (y >= ST7789_HEIGHT))
    return;
  if ((x + w - 1) >= ST7789_WIDTH)
    return;
  if ((y + h - 1) >= ST7789_HEIGHT)
    return;

  ST7789_SetAddressWindow(x, y, x + w - 1, y + h - 1);

  #ifdef USE_DMA
  setDMAMemMode(mem_increase, mode_16bit);                                                            // Set SPI and DMA to 16 bit, enable memory increase
  #else
  setSPI_Size(mode_16bit);                                                                            // Set SPI to 16 bit
  #endif
  ST7789_WriteData((uint8_t*)data, w*h);
  }

/**
 * @brief Accelerated line draw using filling (Only for vertical/horizontal lines)
 * @param x1&y1 -> coordinate of the start point
 * @param x2&y2 -> coordinate of the end point
 * @param color -> color of the line to Draw
 * @return none
 */
int8_t ST7789_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {

  if(x0==x1){                                   // If horizontal
    if(y1<y0){
      int16_t temp = y0;
      y0=y1;
      y1=temp;
    }
  }
  else if(y0==y1){                              // If vertical
    if(x1<x0){
      int16_t temp = x0;
      x0=x1;
      x1=temp;
    }
  }
  else{                                         // Else, return fail, draw using software
    return UG_RESULT_FAIL;
  }

  ST7789_Fill(x0,y0,x1,y1,color);               // Draw using acceleration
  return UG_RESULT_OK;
}
void ST7789_PutChar(uint16_t x, uint16_t y, char ch, const UG_FONT* font, uint16_t color, uint16_t bgcolor){
  UG_FontSelect(font);
  UG_PutChar(ch, x, y, color, bgcolor);
}

void ST7789_PutStr(uint16_t x, uint16_t y,  char *str, const UG_FONT* font, uint16_t color, uint16_t bgcolor){
  UG_FontSelect(font);
  UG_SetForecolor(color);
  UG_SetBackcolor(bgcolor);
  UG_PutString(x, y, str);
}
/**
 * @brief Invert Fullscreen color
 * @param invert -> Whether to invert
 * @return none
 */
void ST7789_InvertColors(uint8_t invert)
{
  ST7789_WriteCommand(invert ? 0x21 /* INVON */ : 0x20 /* INVOFF */);
}

/*
 * @brief Open/Close tearing effect line
 * @param tear -> Whether to tear
 * @return none
 */
void ST7789_TearEffect(uint8_t tear)
{
  ST7789_Select();
  ST7789_WriteCommand(tear ? 0x35 /* TEON */ : 0x34 /* TEOFF */);
  ST7789_UnSelect();
}

/**
 * @brief Initialize ST7789 controller
 * @param none
 * @return none
 */
void ST7789_Init(void)
{
  ST7789_UnSelect();
  ST7789_RST_Clr();
  HAL_Delay(1);
  ST7789_RST_Set();
  HAL_Delay(120);

  UG_Init(&gui, &ST7789_DrawPixel, &ST7789_FillPixels, ST7789_WIDTH, ST7789_HEIGHT);
  UG_DriverRegister(DRIVER_DRAW_LINE, ST7789_DrawLine);
  UG_DriverRegister(DRIVER_FILL_FRAME, ST7789_Fill);
  UG_DriverRegister(DRIVER_FILL_AREA, ST7789_FillArea);
  UG_DriverRegister(DRIVER_DRAW_BMP, ST7789_DrawImage);
  UG_FontSetHSpace(0);
  UG_FontSetVSpace(0);
  //UG_FontSetTransparency(1);

  ST7789_WriteCommand(ST7789_COLMOD);   //  Set color mode
  ST7789_WriteSmallData(ST7789_COLOR_MODE_16bit);
  ST7789_WriteCommand(0xB2);        //  Porch control
  {
    uint8_t data[] = {0x01, 0x01, 0x00, 0x11, 0x11};            // Minimum porch (7% faster screen refresh rate)   *** Restore normal value if having problems ***
    //uint8_t data[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};          // Standard porch
    ST7789_WriteData(data, sizeof(data));
  }
  ST7789_SetRotation(ST7789_ROTATION);  //  MADCTL (Display Rotation)

  /* Internal LCD Voltage generator settings */
  ST7789_WriteCommand(0XB7);        //  Gate Control
  ST7789_WriteSmallData(0x35);      //  Default value
  ST7789_WriteCommand(0xBB);        //  VCOM setting
  ST7789_WriteSmallData(0x19);      //  0.725v (default 0.75v for 0x20)
  ST7789_WriteCommand(0xC0);        //  LCMCTRL
  ST7789_WriteSmallData (0x2C);     //  Default value
  ST7789_WriteCommand (0xC2);       //  VDV and VRH command Enable
  ST7789_WriteSmallData (0x01);     //  Default value
  ST7789_WriteCommand (0xC3);       //  VRH set
  ST7789_WriteSmallData (0x12);     //  +-4.45v (default +-4.1v for 0x0B)
  ST7789_WriteCommand (0xC4);       //  VDV set
  ST7789_WriteSmallData (0x20);     //  Default value
  ST7789_WriteCommand (0xC6);       //  Frame rate control in normal mode
  ST7789_WriteSmallData (0x01);     //  Max refresh rate (111Hz).           *** Restore normal value if having problems ***
  //ST7789_WriteSmallData (0x0F);     //  Default refresh rate (60Hz)
  ST7789_WriteCommand (0xD0);       //  Power control
  ST7789_WriteSmallData (0xA4);     //  Default value
  ST7789_WriteSmallData (0xA1);     //  Default value a1
  /**************** Division line ****************/

  ST7789_WriteCommand(0xE0);
  {
    uint8_t data[] = {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23};
    ST7789_WriteData(data, sizeof(data));
  }

  ST7789_WriteCommand(0xE1);
  {
    uint8_t data[] = {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23};
    ST7789_WriteData(data, sizeof(data));
  }
  ST7789_WriteCommand (ST7789_INVON);   //  Inversion ON
  ST7789_WriteCommand (ST7789_SLPOUT);  //  Out of sleep mode
  ST7789_WriteCommand (ST7789_NORON);   //  Normal Display on

  UG_FillScreen(C_BLACK);             //  Fill
  ST7789_WriteCommand (ST7789_DISPON);  //  Main screen turned on

}


static uint32_t draw_time=0;
static void printTime(void){
  char str[8];
  sprintf(str,"%lums",HAL_GetTick()-draw_time);
  UG_FontSelect(&FONT_12X16);
  UG_SetForecolor(C_WHITE);
  UG_SetBackcolor(C_BLACK);
  UG_PutString(160, 119, str);
}

/** 
 * @brief A Simple test function for ST7789
 * @param  none
 * @return  none
 */

void window_1_callback(UG_MESSAGE *msg);
void ST7789_Test(void)
{
  UG_FillScreen(C_WHITE);
  ST7789_PutStr(10, 10, "Starting Test", &FONT_12X16, C_RED, C_WHITE);

  HAL_Delay(1000);
  uint8_t r=0,g=0,b=0;
  for(r=0; r<32;r++){                                       // R++, G=0, B=0
    UG_FillScreen((uint16_t)r<<11 | g<<5 | b);
  }
  r=31;
  for(g=0; g<64;g+=2){                                      // R=31, G++, B=0
    UG_FillScreen((uint16_t)r<<11 | g<<5 | b);
  }
  g=63;
  for(r=28; r;r--){                                         // R--, Gmax, B=0
    UG_FillScreen((uint16_t)r<<11 | g<<5 | b);
  }
  for(b=0; b<32;b++){                                       // R=0, Gmax, B++
    UG_FillScreen((uint16_t)r<<11 | g<<5 | b);
  }
  b=31;
  for(g=56; g;g-=2){                                        // R=0, G--, Bmax
    UG_FillScreen((uint16_t)r<<11 | g<<5 | b);
  }
  for(r=0; r<32;r++){                                       // R++, G=0, Bmax
    UG_FillScreen((uint16_t)r<<11 | g<<5 | b);
  }
  r=31;
  for(g=0; g<64;g+=2){                                      // Rmax, G++, Bmax
    UG_FillScreen((uint16_t)r<<11 | g<<5 | b);
  }


  UG_FillScreen(C_RED);
  HAL_Delay(500);
  UG_FillScreen(C_GREEN);
  HAL_Delay(500);
  UG_FillScreen(C_BLUE);
  HAL_Delay(500);
  UG_FillScreen(C_BLACK);
  HAL_Delay(500);
  draw_time=HAL_GetTick();
  UG_FillScreen(C_WHITE);
  printTime();


  ST7789_PutStr(10, 10, "Fill Time", &FONT_12X16, C_RED, C_WHITE);
  HAL_Delay(2000);

  UG_FillScreen(C_BLACK);
  ST7789_PutStr(10, 10, "Font test.", &FONT_12X16, C_AZURE, C_BLACK);
  draw_time=HAL_GetTick();
  ST7789_PutStr(10, 30, "Hello Steve!", &FONT_12X16, C_CYAN, C_BLACK);
  ST7789_PutStr(10, 80, "Hello Steve!", &FONT_12X16, C_LIME_GREEN, C_BLACK);
  ST7789_PutStr(10, 55, "Hello Steve!", &FONT_12X16, C_ORANGE_RED, C_BLACK);
  ST7789_PutStr(10, 105, "Hello Steve!", &FONT_12X16, C_HOT_PINK, C_BLACK);
  printTime();
  HAL_Delay(3000);
  UG_FillScreen(C_RED);
  ST7789_PutStr(10, 10, "Line.", &FONT_12X16, C_YELLOW, C_RED);
  draw_time=HAL_GetTick();
  UG_DrawLine(30, 30, 30, 100, C_WHITE);
  UG_DrawLine(30, 30, 100, 30, C_WHITE);
  UG_DrawLine(30, 30, 100, 100, C_WHITE);
  printTime();
  HAL_Delay(1000);

  UG_FillScreen(C_RED);
  ST7789_PutStr(10, 10, "Rect.", &FONT_12X16, C_YELLOW, C_RED);
  draw_time=HAL_GetTick();
  UG_DrawFrame(30, 30, 100, 100, C_WHITE);
  printTime();
  HAL_Delay(1000);

  UG_FillScreen(C_RED);
  ST7789_PutStr(10, 10, "Filled Rect.", &FONT_12X16, C_YELLOW, C_RED);
  draw_time=HAL_GetTick();
  UG_FillFrame(30, 30, 100, 100, C_WHITE);
  printTime();
  HAL_Delay(1000);


  UG_FillScreen(C_RED);
  ST7789_PutStr(10, 10, "Circle.", &FONT_12X16, C_YELLOW, C_RED);
  draw_time=HAL_GetTick();
  UG_DrawCircle(65, 65, 35, C_WHITE);
  printTime();
  HAL_Delay(1000);

  UG_FillScreen(C_RED);
  ST7789_PutStr(10, 10, "Filled Cir.", &FONT_12X16, C_YELLOW, C_RED);
  draw_time=HAL_GetTick();
  UG_FillCircle(65, 65, 35, C_WHITE);
  printTime();
  HAL_Delay(1000);

  UG_FillScreen(C_RED);
  ST7789_PutStr(10, 10, "Triangle.", &FONT_12X16, C_YELLOW, C_RED);
  draw_time=HAL_GetTick();
  UG_DrawTriangle(30, 30, 120, 40, 60, 120, C_WHITE);
  printTime();
  HAL_Delay(1000);

  UG_FillScreen(C_RED);
  draw_time=HAL_GetTick();
  ST7789_PutStr(10, 10, "Filled Tri.", &FONT_12X16, C_YELLOW, C_RED);
  UG_FillTriangle(30, 30, 120, 40, 60, 120, C_WHITE);
  printTime();
  HAL_Delay(1000);

  UG_FillScreen(C_RED);
  ST7789_PutStr(10, 10, "Mesh.", &FONT_12X16, C_YELLOW, C_RED);
  draw_time=HAL_GetTick();
  UG_DrawMesh(30, 30, 100, 100, C_WHITE);
  printTime();
  HAL_Delay(1000);

  UG_FillScreen(C_BLACK);

  #define MAX_OBJECTS 2

  UG_WINDOW window_1;
  UG_BUTTON button_1;
  UG_TEXTBOX textbox_1;
  UG_OBJECT obj_buff_wnd_1[MAX_OBJECTS];

  // Create the window
  UG_WindowCreate(&window_1, obj_buff_wnd_1, MAX_OBJECTS, window_1_callback);
  // Window Title
  UG_WindowSetTitleText(&window_1, "Test Window");      //  \xhh : Special CHR the ASCII value is given by hh interpreted as a hexadecimal number (Check FONT Table)
  UG_WindowSetTitleTextFont(&window_1, &FONT_12X16);
  UG_WindowSetXStart(&window_1, 5);
  UG_WindowSetYStart(&window_1, 5);
  UG_WindowSetXEnd(&window_1, 230);       // Window 450x250
  UG_WindowSetYEnd(&window_1, 130);

  // Create Buttons
  UG_ButtonCreate(&window_1, &button_1, BTN_ID_0, 10, 20, 120, 50);
  //Label Buttons
  UG_ButtonSetFont(&window_1,BTN_ID_0,&FONT_12X16);
  UG_ButtonSetForeColor(&window_1,BTN_ID_0, C_BLACK);
  UG_ButtonSetText(&window_1,BTN_ID_0,"Button");

  // Create Textbox
  UG_TextboxCreate(&window_1, &textbox_1, TXB_ID_0, 10, 60, 200, 100);
  UG_TextboxSetFont(&window_1, TXB_ID_0, &FONT_12X16);
  UG_TextboxSetText(&window_1, TXB_ID_0, "Some text");
  UG_TextboxSetForeColor(&window_1, TXB_ID_0, C_BLACK);
  UG_TextboxSetAlignment(&window_1, TXB_ID_0, ALIGN_CENTER);

  UG_WindowShow(&window_1);
  UG_Update();
  HAL_Delay(3000);

  //  If program doesn't fit in the FLASH, please disable this code:
  //->>
  UG_FillScreen(C_RED);
  draw_time=HAL_GetTick();
  UG_DrawBMP((ST7789_WIDTH-fry.width)/2, (ST7789_HEIGHT-fry.height)/2, &fry);
  printTime();
  UG_FontSetTransparency(1);
  ST7789_PutStr(10, 10, "Image.", &FONT_12X16, C_YELLOW, C_RED);
  UG_FontSetTransparency(0);
  //<<-

  HAL_Delay(3000);
}


void window_1_callback(UG_MESSAGE *msg)
{
/*
    if(msg->type == MSG_TYPE_OBJECT)
    {
        if(msg->id == OBJ_TYPE_BUTTON)
        {
            if(msg->event == OBJ_EVENT_PRESSED)
            {
                switch(msg->sub_id)
                {
                    case BTN_ID_0:
                    {
                        LED4_Write(0);
                        UG_ButtonHide(&window_1,BTN_ID_1);
                        break;
                    }
                    case BTN_ID_1:
                    {
                        UG_TextboxSetText(&window_1, TXB_ID_0, "Pressed!");
                        break;
                    }
                    case BTN_ID_2:
                    {
                        LED4_Write(1);
                        UG_ButtonShow(&window_1,BTN_ID_1);
                        break;
                    }
                    case BTN_ID_3:
                    {
                        UG_TextboxSetText(&window_1, TXB_ID_0, "Pressed!");
                        LED4_Write(!LED4_Read());
                        break;
                    }
                }
            }
            if(msg->event == OBJ_EVENT_RELEASED)
            {
                if(msg->sub_id == BTN_ID_1)
                {
                        UG_TextboxSetText(&window_1, TXB_ID_0, "This is a \n test sample window!");
                }
                if(msg->sub_id == BTN_ID_3)
                {
                        UG_TextboxSetText(&window_1, TXB_ID_0, "This is a \n test sample window!");
                }
            }
        }
    }
*/
}

