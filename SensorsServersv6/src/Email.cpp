#ifdef _USEEMAIL

#include "Email.hpp"

void initEmailConfig() {
    EmailConfig.SMTP_HOST = "smtp.gmail.com";
    EmailConfig.SMTP_PORT = 465;
    EmailConfig.AUTHOR_EMAIL = "your_sender_email@gmail.com";
    EmailConfig.AUTHOR_PASSWORD = "YOUR_16_DIGIT_APP_PASSWORD";
    EmailConfig.RECIPIENT_EMAIL1 = "recipient_email@example.com";
    EmailConfig.RECIPIENT_EMAIL2 = "recipient_email2@example.com";
    EmailConfig.RECIPIENT_EMAIL3 = "recipient_email3@example.com";
}

#endif