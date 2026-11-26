#include "validation.hpp"
int main(){
 json t={{"title","Rede"},{"description","Falha"},{"customer","Cliente"},{"priority","normal"},{"status","open"},{"assigned_to",nullptr}};
 if(!valid_ticket(t))return 1;
 auto a=t;a["title"]=" ";if(valid_ticket(a))return 2;
 a=t;a["priority"]="x";if(valid_ticket(a))return 3;
 a=t;a["assigned_to"]=-1;if(valid_ticket(a))return 4;
 a=t;a["assigned_to"]=true;if(valid_ticket(a))return 5;
 return 0;
}
