#include <xuartlite_l.h>
#include <xgpio.h>
#include <xintc_l.h>
#include <xuartlite.h>
#include <xparameters.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

XGpio gpio;

typedef struct buffered_tx {
  char *string;
  int tx_posn;
  struct buffered_tx *next;
} buffered_tx_t;

buffered_tx_t *tx_queue_0 = NULL;
buffered_tx_t *tx_queue_1 = NULL;

// Is the TX Buffer Full?
int tx_buffer_full(void *baseaddr_p) {
	return XUartLite_IsTransmitFull(baseaddr_p);
}

int rx_buffer_empty(void *baseaddr_p) {
	return XUartLite_IsReceiveEmpty(baseaddr_p);
}

// TX a single character
void tx_send_char(char c, void *baseaddr_p) {
//	XUartLite_SendByte(baseaddr_p, c);
	XUartLite_WriteReg(baseaddr_p, XUL_TX_FIFO_OFFSET, c);
}

// Identify the TX queue by UART base address
buffered_tx_t **queue_select(void *baseaddr_p) {
	return baseaddr_p == XPAR_AXI_UARTLITE_0_BASEADDR ? &tx_queue_0 : &tx_queue_1;
}

// UART0 ISR
void uart_0_int_handler(void *baseaddr_p) {
	if (!tx_buffer_full(baseaddr_p)) {
		enqueue_tx(tx_queue_0, baseaddr_p);
	}
	if (!rx_buffer_empty(baseaddr_p)) {
		char string[2];
		string[0] = XUartLite_ReadReg(baseaddr_p, XUL_RX_FIFO_OFFSET);
		string[1] = 0;
		uart_buffered_tx(string, baseaddr_p);
	}
}

// UART1 ISR
void uart_1_int_handler(void *baseaddr_p) {
	if (!tx_buffer_full(baseaddr_p)) {
		enqueue_tx(tx_queue_1, baseaddr_p);
	}
}

// Transmit until full
// Cleanup and increment to next message
void enqueue_tx(buffered_tx_t *tx, void *baseaddr_p) {
  if (tx == NULL) { return; }

  int strLen = strlen(tx->string);

  // Transmit until buffer is full
  while (tx != NULL && tx->tx_posn < strLen && !tx_buffer_full(baseaddr_p)) {
    tx_send_char(tx->string[tx->tx_posn++], baseaddr_p);
  }

  // End of this message: Move on to the next one if available
  if (tx->tx_posn == strLen) {
	buffered_tx_t **tx_queue = queue_select(baseaddr_p);
    *tx_queue = tx->next;
    free(tx->string);
    free(tx);
  }

  return;
}

// Transmit to the UART
// Buffered to send larger amounts of data
void uart_buffered_tx(unsigned char *string, void *baseaddr_p) {
	// Create a new message and copy the string
	buffered_tx_t *tail = (buffered_tx_t*) calloc(sizeof(buffered_tx_t), 1);
	tail->string = calloc(strlen(string) + 1, 1);
	strncpy(tail->string, string, strlen(string));

	buffered_tx_t **tx_queue = queue_select(baseaddr_p);

	// Append the new message to the end of the queue
	if (*tx_queue == NULL) {
		*tx_queue = tail;
	} else {
		buffered_tx_t *iter = *tx_queue;
		while (iter->next) { iter = iter->next; }
		iter->next = tail;
	}

	// Start transmit if this is the first message
	if (tail == *tx_queue) { enqueue_tx(tail, baseaddr_p); }
}

int main(void)
{
	// Write something to GPIO
 	XGpio_Initialize(&gpio, XPAR_AXI_GPIO_0_DEVICE_ID);
	XGpio_SetDataDirection(&gpio, 1, 0xFFFFFFF0);
	XGpio_DiscreteWrite(&gpio, 1, 0x0E);

	/* Enable MicroBlaze exception */
	microblaze_enable_interrupts();

	/* Connect uart interrupt handler that will be called when an interrupt
	 * for the uart occurs*/
	XIntc_RegisterHandler(
			XPAR_INTC_0_BASEADDR,
			XPAR_MICROBLAZE_0_AXI_INTC_AXI_UARTLITE_0_INTERRUPT_INTR,
			(XInterruptHandler)uart_0_int_handler,
			(void *)XPAR_AXI_UARTLITE_0_BASEADDR
	);

	// Connect UART 1 interrupt handler
	XIntc_RegisterHandler(
			XPAR_INTC_0_BASEADDR,
			XPAR_MICROBLAZE_0_AXI_INTC_AXI_UARTLITE_1_INTERRUPT_INTR,
			(XInterruptHandler)uart_1_int_handler,
			(void *)XPAR_AXI_UARTLITE_1_BASEADDR
	);

	/* Start the interrupt controller */
	XIntc_MasterEnable(XPAR_INTC_0_BASEADDR);

	/* Enable uart interrupt in the interrupt controller */
	XIntc_EnableIntr(XPAR_INTC_0_BASEADDR, XPAR_AXI_UARTLITE_0_INTERRUPT_MASK | XPAR_AXI_UARTLITE_1_INTERRUPT_MASK);

	/* Enable Uartlite interrupt */
	XUartLite_EnableIntr(XPAR_AXI_UARTLITE_0_BASEADDR);
	XUartLite_EnableIntr(XPAR_AXI_UARTLITE_1_BASEADDR);

	uart_buffered_tx("Hello world! I'm a super long string! Transmit me, please!\r\n", XPAR_AXI_UARTLITE_0_BASEADDR);
	uart_buffered_tx("One sec, gotta transmit a message on UART1.\r\n", XPAR_AXI_UARTLITE_1_BASEADDR);
	uart_buffered_tx("Alright, I'm back, transmitting on UART0.\r\n", XPAR_AXI_UARTLITE_0_BASEADDR);

	/* Wait for interrupts to occur */
	while(1);
}
