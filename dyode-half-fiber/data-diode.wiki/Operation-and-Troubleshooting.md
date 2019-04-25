> NOTE: DYODE sends from `inbox` to `outbox`.

### On Highside Only

Launch on highside
`python dyode_in.py`

### On Lowside Only

Launch on lowside
`python dyode_out.py`


## Troubleshooting

Before sending large files, test the connection with smaller files.

Ensure you have actually set a static ARP. If you did not set the ARP the way outlined here, the address will be forgotten upon reboot every time.