/***************************************************************************//**
 * @file
 *   i2c.c
 * @author
 *   Frank McDermott
 * @date
 *   11/06/2022
 * @brief
 *   I2C source file for use with the Si7021 Temperature & Humidity Sensor
 ******************************************************************************/

//***********************************************************************************
// included header file
//***********************************************************************************
#include "i2c.h"


//***********************************************************************************
// static/private data
//***********************************************************************************
static volatile I2C_SM_STRUCT i2c0_sm;
static volatile I2C_SM_STRUCT i2c1_sm;


//***********************************************************************************
// static/private functions
//***********************************************************************************
static void i2c_bus_reset(I2C_TypeDef *i2c);
static void i2cn_ack_sm(volatile I2C_SM_STRUCT *i2c_sm);
static void i2cn_nack_sm(volatile I2C_SM_STRUCT *i2c_sm);
static void i2cn_rxdata_sm(volatile I2C_SM_STRUCT *i2c_sm);
static void i2cn_mstop_sm(volatile I2C_SM_STRUCT *i2c_sm);


//***********************************************************************************
// function definitions
//***********************************************************************************
/***************************************************************************//**
 * @brief
 *  Resets the I2C Bus
 *
 * @details
 *  A reset is achieved by aborting any current operations on the I2C bus to
 *  for the bus to go idle, saving the state of the IEN register, disabling
 *  all interrupts, clearing all interrupt flags, clearing the transmit
 *  buffer and MSTOP bit, sending a START and STOP command, and finally
 *  restoring the state of the IEN register.
 *
 * @param[in] i2c
 *  Desired I2Cn peripheral (either I2C0 or I2C1)
 ******************************************************************************/
void i2c_bus_reset(I2C_TypeDef *i2c)
{
  // local variable to save the state of the IEN register
  uint32_t ien_state;

  // abort current transmission to make bus go idle (TRM 16.5.2)
  i2c->CMD = I2C_CMD_ABORT;

  // save state of IEN register
  ien_state = i2c->IEN;

  // disable all interrupts (16.5.17)
  i2c->IEN = _I2C_IEN_RESETVALUE;

  // clear IFC register (TRM 16.5.16)
  i2c->IFC = _I2C_IFC_MASK;

  // assert that IF register is clear
  EFM_ASSERT(!(i2c->IF & _I2C_IEN_RESETVALUE));

  // clear the transmit buffer (16.5.2)
  i2c->CMD = I2C_CMD_CLEARTX;

  // clear MSTOP bit prior to bus reset
  i2c->IFC |= I2C_IFC_MSTOP;

  // bus reset (TRM 16.3.12.2)
  i2c->CMD = (I2C_CMD_START | I2C_CMD_STOP);

  // ensure reset occurred properly
  while(!(i2c->IF & I2C_IF_MSTOP));

  // clear IFC register (TRM 16.5.16)
  // clear any bits that may have been generated by START/STOP
  i2c->IFC = _I2C_IFC_MASK;

  // reset I2C peripheral by setting ABORT bit in CMD register
  i2c->CMD = I2C_CMD_ABORT;

  // restore IEN register
  i2c->IEN = ien_state;
}


/***************************************************************************//**
 * @brief
 *  Opens the I2C peripheral.
 *
 * @details
 *  Enables the proper I2Cn CMU clock, sets the START bit, initializes
 *  I2C, routes & enables the I2C to the proper pin, and resets the I2C bus.
 *
 * @param[in] i2c
 *  Desired I2Cn peripheral (either I2C0 or I2C1)
 *
 * @param[in] app_i2c_open
 *  All data required to open the I2C peripheral encapsulated in struct
 ******************************************************************************/
void i2c_open(I2C_TypeDef *i2c, I2C_OPEN_STRUCT *app_i2c_open)
{
  // instantiate a local I2C_Init struct
  I2C_Init_TypeDef i2c_init_values;

  // if the address of i2c is equal to the base address of the
  // I2C0 base peripheral ...
  if(i2c == I2C0)
  {
      // ... enable I2C0 clock
      CMU_ClockEnable(cmuClock_I2C0, true);
  }

  // if the address of i2c is equal to the base address of the
  // I2C1 base peripheral ...
  if(i2c == I2C1)
  {
      // ... enable I2C1 clock
      CMU_ClockEnable(cmuClock_I2C1, true);
  }


  // if START interrupt flag not set ...
  if(!(i2c->IF & I2C_IFS_START))
  {
      // .. set the START interrupt flag
      i2c->IFS = I2C_IFS_START;

      // assert that the flag has been set
      EFM_ASSERT(i2c->IF & I2C_IFS_START);
  }
  // .. else ...
  else
  {
      // clear START flag
      i2c->IFC = I2C_IFC_START;

      // assert that the flag as been cleared
      EFM_ASSERT(!(i2c->IF & I2C_IFS_START));
  }

  // set values for I2C_Init
  i2c_init_values.enable = app_i2c_open->enable;
  i2c_init_values.master = app_i2c_open->master;
  i2c_init_values.freq = app_i2c_open->freq;
  i2c_init_values.refFreq = app_i2c_open->refFreq;
  i2c_init_values.clhr = app_i2c_open->clhr;

  // initialize I2C peripheral
  I2C_Init(i2c, &i2c_init_values);

  // set route location for SDA and SCL
  i2c->ROUTELOC0 |= app_i2c_open->sda_loc;
  i2c->ROUTELOC0 |= app_i2c_open->scl_loc;

  // enable pin route
  i2c->ROUTEPEN |= app_i2c_open->sda_pen;
  i2c->ROUTEPEN |= app_i2c_open->scl_pen;

  // reset the I2C bus
  i2c_bus_reset(i2c);
}


/***************************************************************************//**
 * @brief
 *  Start the I2C peripheral.
 *
 * @details
 *  Initializes and starts the I2C state machine. Can be used with either
 *  the I2C0 or I2C1 peripheral.
 *
 * @param[in] i2c
 *  Pointer to desired I2Cn peripheral (either I2C0 or I2C1)
 *
 * @param[in] slave_addr
 *  Address of the slave device
 *
 * @param[in] data
 *   Pointer to data storage location
 *
 * @param[in] rw
 *   rw = 0 indicates read mode; rw = 1 indicates write mode.
 *
 * @param[in] device_cb
 *  slave device's callback value. Used to schedule the corresponding callback
 *  function after the I2C transaction is complete
 * @param[in] bytes_req
 *  number of bytes requested
 ******************************************************************************/
void i2c_init_sm(volatile I2C_SM_STRUCT *i2c_sm)
{
  // the I2C peripheral cannot cannot go below EM2
  sleep_block_mode(I2C_EM_BLOCK);

  // atomic operation
  CORE_DECLARE_IRQ_STATE;
  CORE_ENTER_CRITICAL();

  // set busy bit
  i2c_sm->busy = I2C_BUS_BUSY;

  // enable interrupts
  i2c_sm->I2Cn->IEN = I2C_IEN_MASK;

  // if starting the I2C0 peripheral ...
  if(i2c_sm->I2Cn == I2C0)
  {
      // halt until bus is ready
      while(i2c0_sm.busy);

      // will trigger if a previous I2C operation has not completed
      EFM_ASSERT((I2C0->STATE & _I2C_STATE_STATE_MASK) == I2C_STATE_STATE_IDLE);

      i2c0_sm = *i2c_sm;
      NVIC_EnableIRQ(I2C0_IRQn);
  }


  // if starting the I2C1 peripheral ...
  if(i2c_sm->I2Cn == I2C1)
  {
      // halt until bus is ready
      while(i2c1_sm.busy);

      // will trigger if a previous I2C operation has not completed
      EFM_ASSERT((I2C1->STATE & _I2C_STATE_STATE_MASK) == I2C_STATE_STATE_IDLE);

      i2c1_sm = *i2c_sm;
      NVIC_EnableIRQ(I2C1_IRQn);
  }

  // 80ms timer delay to ensure RWM sync
  timer_delay(I2C_80MS_DELAY);

  CORE_EXIT_CRITICAL();

}


void i2c_tx_start(volatile I2C_SM_STRUCT *i2c_sm, I2C_RW_Typedef rw)
{
  // send start bit
  i2c_sm->I2Cn->CMD = I2C_CMD_START;

  // construct 8-bit read/write header packet.
  // 7 MSB = slave device's address
  // LSB   =  read/write bit
  uint32_t r_w_header = ((i2c_sm->slave_addr << 1) | rw);

  // transmit header packet
  *i2c_sm->txdata = r_w_header;
}


void i2c_tx_stop(I2C_SM_STRUCT *i2c_sm)
{
  // set stop bit in I2C CMD register
  i2c_sm->I2Cn->CMD = I2C_CMD_STOP;
}


void i2c_tx_cmd(I2C_SM_STRUCT *i2c_sm, uint32_t tx_cmd)
{
  // transmit command via TXDATA
  *i2c_sm->txdata = tx_cmd;
}


/***************************************************************************//**
 * @brief
 *  I2C0 peripheral IRQ Handler
 *
 * @details
 *  Handles ACK, NACK, RXDATAV, and MSTOP interrupts for the I2C0 peripheral
 ******************************************************************************/
void I2C0_IRQHandler(void)
{
  // save flags that are both enabled and raised
  uint32_t intflags = (I2C0->IF & I2C0->IEN);

  // lower flags
  I2C0->IFC = intflags;

  // handle ACK
  if(intflags & I2C_IF_ACK)
  {
      i2cn_ack_sm(&i2c0_sm);
  }

  // handle NACK
  if(intflags & I2C_IF_NACK)
  {
      i2cn_nack_sm(&i2c0_sm);
  }

  // handle RXDATAV
  if(intflags & I2C_IF_RXDATAV)
  {
      i2cn_rxdata_sm(&i2c0_sm);
  }

  // handle MSTOP
  if(intflags & I2C_IF_MSTOP)
  {
      i2cn_mstop_sm(&i2c0_sm);
  }
}


/***************************************************************************//**
 * @brief
 *  I2C1 peripheral IRQ Handler
 *
 * @details
 *  Handles ACK, NACK, RXDATAV, and MSTOP interrupts for the I2C1 peripheral
 ******************************************************************************/
void I2C1_IRQHandler(void)
{
  // save flags that are both enabled and raised
    uint32_t intflags = (I2C1->IF & I2C1->IEN);

    // lower flags
    I2C1->IFC = intflags;

    // handle ACK
    if(intflags & I2C_IF_ACK)
    {
      i2cn_ack_sm(&i2c1_sm);
    }

    // handle NACK
    if(intflags & I2C_IF_NACK)
    {
        i2cn_nack_sm(&i2c1_sm);
    }

    // handle RXDATA
    if(intflags & I2C_IF_RXDATAV)
    {
        i2cn_rxdata_sm(&i2c1_sm);
    }

    // handle MSTOP
    if(intflags & I2C_IF_MSTOP)
    {
        i2cn_mstop_sm(&i2c1_sm);
    }
}


/***************************************************************************//**
 * @brief
 *  I2C ACK state machine
 *
 * @details
 *  State machine function for an ACK interrupt. Functionality depends on the
 *  current state. Handles ACKs for the Request Resource, Command Transmit,
 *  and Data Request states.
 *
 * @param[in] i2c_sm
 *  Static state machine struct which corresponds to the desired I2Cn
 *  peripheral
 ******************************************************************************/
void i2cn_ack_sm(volatile I2C_SM_STRUCT *i2c_sm)
{
  // make atomic by disallowing interrupts
  CORE_DECLARE_IRQ_STATE;
  CORE_ENTER_CRITICAL();

  switch(i2c_sm->curr_state)
  {
    case req_res:
      // send command to measure relative humidity (no hold master mode)
      *i2c_sm->txdata = (measure_RH_NHMM);

      // change state
      i2c_sm->curr_state = command_tx;
      break;
    case command_tx:
      // send repeated start command
      i2c_sm->I2Cn->CMD = I2C_CMD_START;

      // send slave addr + read bit
      *i2c_sm->txdata = ((i2c_sm->slave_addr << I2C_ADDR_RW_SHIFT) | SI7021_I2C_READ);
      // change state
      i2c_sm->curr_state = data_req;
      break;
    case data_req:
      // change state
      i2c_sm->curr_state = data_rx;
      break;
    default:
      EFM_ASSERT(false);
      break;
  }

  // 80ms timer delay for RWM sync
  timer_delay(I2C_80MS_DELAY);

  // exit core critical to allow interrupts
  CORE_EXIT_CRITICAL();
}


/***************************************************************************//**
 * @brief
 *  I2C NACK state machine
 *
 * @details
 *  State machine function for a NACK interrupt. Functionality depends on the
 *  current state. Handles NACKs for the Request Resource, Command Transmit,
 *  and Data Request states.
 *
 * @param[in] i2c_sm
 *  Static state machine struct which corresponds to the desired I2Cn
 *  peripheral
 ******************************************************************************/
void i2cn_nack_sm(volatile I2C_SM_STRUCT *i2c_sm)
{
  // make atomic by disallowing interrupts
  CORE_DECLARE_IRQ_STATE;
  CORE_ENTER_CRITICAL();

  switch(i2c_sm->curr_state)
  {
    case req_res:
      // send repeated start command
      i2c_sm->I2Cn->CMD = I2C_CMD_START;

      // re-send slave addr + write bit
      *i2c_sm->txdata = (i2c_sm->slave_addr | SI7021_I2C_WRITE);
      break;
    case command_tx:
      // send CONT command
      i2c_sm->I2Cn->CMD = I2C_CMD_CONT;

      // re-send command to measure relative humidity (no hold master mode)
      *i2c_sm->txdata = (measure_RH_NHMM);
      break;
    case data_req:
      // re-send repeated start command
      i2c_sm->I2Cn->CMD = I2C_CMD_START;

      // re-send slave addr + read bit
      *i2c_sm->txdata = (i2c_sm->slave_addr | SI7021_I2C_READ);
      break;
    default:
      EFM_ASSERT(false);
  }

  // 80ms timer delay for RWM sync
  timer_delay(I2C_80MS_DELAY);

  // exit core critical to allow interrupts
  CORE_EXIT_CRITICAL();
}


/***************************************************************************//**
 * @brief
 *  I2C RXDATA state machine
 *
 * @details
 *  State machine function for an RXDATAV interrupt. Functionality depends
 *  on the current state. Handles RXDATAVs for the Data Request state
 *
 * @param[in] i2c_sm
 *  Static state machine struct which corresponds to the desired I2Cn
 *  peripheral
 ******************************************************************************/
void i2cn_rxdata_sm(volatile I2C_SM_STRUCT *i2c_sm)
{
  // make atomic by disallowing interrupts
  CORE_DECLARE_IRQ_STATE;
  CORE_ENTER_CRITICAL();

  switch(i2c_sm->curr_state)
  {
    case data_rx:
      // decrement num_bytes counter
      i2c_sm->num_bytes--;

      // retrieve read data, but left shift that data n number of bytes remaining
      *i2c_sm->data |= (*i2c_sm->rxdata << (MSBYTE_SHIFT * i2c_sm->num_bytes));

      // check if more data is expected ...
      if(i2c_sm->num_bytes > 0)
      {
          // send ACK
          i2c_sm->I2Cn->CMD = I2C_CMD_ACK;
      }
      else
      {
          // send NACK
          i2c_sm->I2Cn->CMD = I2C_CMD_NACK;

          // change state
          i2c_sm->curr_state = m_stop;

          //send STOP
          i2c_sm->I2Cn->CMD = I2C_CMD_STOP;
      }

      break;
    default:
      break;
  }

  // 80ms timer delay for RWM sync
  timer_delay(I2C_80MS_DELAY);

  // exit core critical to allow interrupts
  CORE_EXIT_CRITICAL();
}


/***************************************************************************//**
 * @brief
 *  I2C MSTOP state machine
 *
 * @details
 *  State machine function for an MSTOP. Functionality depends on the
 *  current state. Handles MSTOPs for the MSTOP state. Since this is the
 *  end of an I2C transaction this function also releases the bus, unblocks
 *  EM2, and schedules the Humidity Read callback event.
 *
 * @param[in] i2c_sm
 *  Static state machine struct which corresponds to the desired I2Cn
 *  peripheral
 ******************************************************************************/
void i2cn_mstop_sm(volatile I2C_SM_STRUCT *i2c_sm)
{
  // make atomic by disallowing interrupts
  CORE_DECLARE_IRQ_STATE;
  CORE_ENTER_CRITICAL();

  switch(i2c_sm->curr_state)
  {
    case m_stop:
      // clear I2C State Machine busy bit
      i2c_sm->busy = I2C_BUS_READY;

      // unblock sleep
      sleep_unblock_mode(I2C_EM_BLOCK);

      // schedule humidity read call back even
      add_scheduled_event(i2c_sm->i2c_cb);

      // reset the I2C bus
      i2c_bus_reset(i2c_sm->I2Cn);
      break;
    default:
      EFM_ASSERT(false);
  }

  // 80ms timer delay for RWM sync
  timer_delay(I2C_80MS_DELAY);

  // exit core critical to allow interrupts
  CORE_EXIT_CRITICAL();
}
