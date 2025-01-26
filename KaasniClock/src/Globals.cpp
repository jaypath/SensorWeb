#include "Globals.hpp"

struct Screen myScreen;

String breakString(String *inputstr,String token) //take a pointer to input string and break it to the token, and shorten the input string at the same time. Remove token used.
{
  String outputstr = "";
  String orig = *inputstr;

  int strOffset = orig.indexOf(token,0);
  if (strOffset == -1) { //did not find the comma, we may have cut the last one. abort.
    return outputstr; //did not find the token, just return nothing and do not touch inputstr
  } else {
    outputstr= orig.substring(0,strOffset); //end index is NOT inclusive
    orig.remove(0,strOffset+1);
  }

  *inputstr = orig;
  return outputstr;

}


