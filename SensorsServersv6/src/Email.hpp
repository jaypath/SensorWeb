#ifdef _USEEMAIL
#ifndef EMAIL_HPP
    #define EMAIL_HPP

    #include "globals.hpp"
    #include <ESP_Mail_Client.h>


Struct STRUCT_EMAILCONFIG {
    String SMTP_HOST; 
    uint16_t SMTP_PORT;
    String AUTHOR_EMAIL;
    String AUTHOR_PASSWORD; 
    String RECIPIENT_EMAIL1; 
    String RECIPIENT_EMAIL2; 
    String RECIPIENT_EMAIL3; 
};

extern STRUCT_EMAILCONFIG EmailConfig;

void initEmailConfig();
void sendEmail(String subject, String body);
void sendEmail(ArborysSnsType* sensor);

#endif
#endif