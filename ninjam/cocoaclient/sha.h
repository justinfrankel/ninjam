#ifndef _WDL_SHA_H_
#define _WDL_SHA_H_


#define WDL_SHA1SIZE 20

// sha

class WDL_SHA1 {

public:
  WDL_SHA1();
  void add(const void *data, int datalen);
  void result(void *out);
  void reset();

private:

  unsigned long H[5];
  unsigned long W[80];
  int lenW;
  unsigned long size[2];
};

#endif

