import("stdfaust.lib");

process = attach(0,
  os.lf_saw(4) : hbargraph("foo",0,1));