#ifndef _SHA_H_
#define _SHA_H_


#define SHA1SIZE 20

// sha

class SHA1 {

public:
  SHA1();
  void add(void *data, int datalen);
  void result(void *out);
  void reset();

private:

  unsigned long H[5];
  unsigned long W[80];
  int lenW;
  unsigned long size[2];
};

#endif