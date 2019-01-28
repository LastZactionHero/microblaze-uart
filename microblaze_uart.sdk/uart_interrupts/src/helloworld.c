#include <xuartlite_l.h>
#include <xgpio.h>
#include <xintc_l.h>
#include <xuartlite.h>
#include <xparameters.h>
#include <string.h>

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

XGpio gpio;

unsigned char *uart_send_buffer;
unsigned int uart_send_buffer_len = 0;
unsigned int uart_tx_idx = 0;

#define UART_TX_BUFFER_DONE (uart_tx_idx == uart_send_buffer_len)



// UART ISR
void uart_int_handler(UINTPTR *baseaddr_p) {
	/* Read from UART until empty */
	while (!XUartLite_IsReceiveEmpty(baseaddr_p)) {
		char c = XUartLite_RecvByte(baseaddr_p); // Read a character
		XUartLite_SendByte(baseaddr_p, c); // Echo
	}

	/* Transmit anything still pending */
	if(!XUartLite_IsTransmitFull(baseaddr_p) && !UART_TX_BUFFER_DONE) {
		uart_buffered_tx_segment(baseaddr_p);
	}
}


// Transmit to the UART
// Buffered to send larger amounts of data
// TODO: Pass in a device ID, use a different buffer for different device IDs
void uart_buffered_tx(unsigned char *buffer, void *baseaddr_p) {
	// Copy to the UART TX buffer
	uart_tx_idx = 0;
	uart_send_buffer_len = strlen(buffer);

	uart_send_buffer = calloc(uart_send_buffer_len + 1, sizeof(unsigned char));
	strncpy(uart_send_buffer, buffer, uart_send_buffer_len);

	uart_buffered_tx_segment(baseaddr_p);
}

// Fill the UART TX FIFO with remaining content to transmit
void uart_buffered_tx_segment(void *baseaddr_p) {
	// Write to the UART TX FIFO buffer until full or we've run out of content
	while (!XUartLite_IsTransmitFull(baseaddr_p) && !UART_TX_BUFFER_DONE) {
		XUartLite_WriteReg(baseaddr_p, XUL_TX_FIFO_OFFSET, uart_send_buffer[uart_tx_idx++]);
	}

	// Nothing left to send? Free the memory and reset
	if (UART_TX_BUFFER_DONE) {
		free(uart_send_buffer);
		uart_tx_idx = 0;
		uart_send_buffer_len = 0;
	}
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
			(XInterruptHandler)uart_int_handler,
			(void *)XPAR_AXI_UARTLITE_0_BASEADDR
	);

	// Connect UART 1 interrupt handler
	XIntc_RegisterHandler(
			XPAR_INTC_0_BASEADDR,
			XPAR_MICROBLAZE_0_AXI_INTC_AXI_UARTLITE_1_INTERRUPT_INTR,
			(XInterruptHandler)uart_int_handler,
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

	/* Wait for interrupts to occur */
	while (1);
}
