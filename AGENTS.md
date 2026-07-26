# Agent 指令

## 构建固件

```bash
make -f ./mico-os/makefiles/Makefile TC1@MK3031@moc
```

## 构建并刷写固件

```bash
sudo make -f ./mico-os/makefiles/Makefile TC1@MK3031@moc download JTAG=jlink_swd run
```
