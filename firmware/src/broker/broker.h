// Calendar broker client: polls the Apps Script web app every 60 s over HTTPS.
#pragma once

void brokerBegin();  // starts the poll task
void brokerKick();   // poll now (settings changed, picker requested)
bool brokerConfigured();
