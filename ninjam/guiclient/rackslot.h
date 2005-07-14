#ifndef _RACKSLOT_H
#define _RACKSLOT_H

// a generic "thing" you can put in a rackwnd

//CUT class BaseWnd;
class RackWnd;

enum {
  RS_TYPE_MASTER=		MK4CC(0,'m','s','t'),
  RS_TYPE_METRONOME=		MK4CC(10,'m','t','r'),
  RS_TYPE_AUDIO=		MK4CC(20,'a','u','d'),
  RS_TYPE_LOCAL_SEPARATOR=	MK4CC(30,'s','e','p'),
  RS_TYPE_LOCAL_CHANNEL=	MK4CC(40,'l','c','h'),
  RS_TYPE_CHAN_SEPARATOR=	MK4CC(50,'s','e','p'),
  RS_TYPE_CHANNEL=		MK4CC(60,'c','h','n'),
  RS_TYPE_SWEEP=		MK4CC(70,'s','w','p'),
};

class RackSlot {
public:
  RackSlot(RackWnd *parrackwnd);

  virtual ~RackSlot() { }

  virtual FOURCC getRackSlotType()=0;

  virtual int getHeight()=0;

  virtual void resizeSlot(RECT *r)=0;

  virtual int getSortVal()=0;

  virtual void onRefresh() { }	// called at like 10-20hz, display your shit

  RackWnd *getRackWnd() { return parrackwnd; }

  static int compareItem(RackSlot *p1, RackSlot* p2) {
    char p1c = (p1->getRackSlotType() >> 24);
    char p2c = (p2->getRackSlotType() >> 24);
    int r = CMP3(p1c, p2c);
    if (r != 0) return r;
    // same type
    r = CMP3(p1->getSortVal(), p2->getSortVal());
    return r ? r : CMP3(p1, p2);	// sort by ptr if equal (for stability)
  }

private:
  RackWnd *parrackwnd;
};

#endif
