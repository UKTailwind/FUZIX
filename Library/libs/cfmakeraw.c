#include <termios.h>
#include <unistd.h>

void cfmakeraw(register struct termios *p)
{
  p->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXANY | IXOFF);
  p->c_oflag &= ~OPOST;
  p->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
  p->c_cflag &= ~(CSIZE | PARENB);
  p->c_cflag |= CS8;
  p->c_cc[VMIN] = 1;
  p->c_cc[VTIME] = 0;
}
