"""Answer a prompt that is not the shell: bootdev, or login.

  python fzboot.py hdb2      answer the boot device prompt
  python fzboot.py root      log in

fzsh.py cannot do either - it waits for "# " and neither prompt is
that, which is why a reflash used to need a terminal by hand."""
import os, sys, time, serial, fzport

ser = fzport.open_port(115200, timeout=1,
                       port=os.environ.get("FZPORT", "COM11"))
time.sleep(0.3)
ser.reset_input_buffer()
ser.write(b"\r")
ser.flush()
time.sleep(0.5)
ser.write((sys.argv[1] if len(sys.argv) > 1 else "hdb2").encode() + b"\r")
ser.flush()
end = time.time() + float(sys.argv[2] if len(sys.argv) > 2 else 25)
while time.time() < end:
    n = ser.in_waiting
    if n:
        sys.stdout.write(ser.read(n).decode("latin1"))
        sys.stdout.flush()
    else:
        time.sleep(0.1)
