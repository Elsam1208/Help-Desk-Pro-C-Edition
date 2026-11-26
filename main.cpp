#include "validation.hpp"
#include <httplib.h>
#include <mysql.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <vector>
#include <cstdlib>
#include <sstream>
using namespace std;
using Clock=chrono::steady_clock;
map<string,string> cfg;
string get(string k,string fallback=""){auto e=getenv(k.c_str());return e?e:cfg.count(k)?cfg[k]:fallback;}
void config(){ifstream f(".env");string s;while(getline(f,s)){if(!s.empty()&&s.back()=='\r')s.pop_back();auto p=s.find('=');if(p!=string::npos&&s[0]!='#')cfg[s.substr(0,p)]=s.substr(p+1);}}
string hex(const unsigned char* p,size_t n){const char* h="0123456789abcdef";string s;for(size_t i=0;i<n;i++){s+=h[p[i]>>4];s+=h[p[i]&15];}return s;}
string random_hex(int n){vector<unsigned char>b(n);if(RAND_bytes(b.data(),n)!=1)throw runtime_error("random");return hex(b.data(),b.size());}
string derive(const string& p,const string& salt){unsigned char b[32];if(!PKCS5_PBKDF2_HMAC(p.data(),int(p.size()),reinterpret_cast<const unsigned char*>(salt.data()),int(salt.size()),600000,EVP_sha256(),32,b))throw runtime_error("hash");return hex(b,32);}
string hashpw(string p){auto s=random_hex(16);return s+":"+derive(p,s);}
bool checkpw(string p,string h){if(h.size()!=97||h[32]!=':')return false;auto d=derive(p,h.substr(0,32));return CRYPTO_memcmp(d.data(),h.data()+33,64)==0;}
struct DB{
 MYSQL* c;
 DB():c(mysql_init(nullptr)){if(!c)throw runtime_error("mysql init");if(!mysql_real_connect(c,get("DB_HOST","127.0.0.1").c_str(),get("DB_USER","root").c_str(),get("DB_PASSWORD").c_str(),get("DB_NAME","helpdesk_cpp").c_str(),stoi(get("DB_PORT","3306")),nullptr,0)){mysql_close(c);throw runtime_error("mysql connection");}if(mysql_set_character_set(c,"utf8mb4")){mysql_close(c);throw runtime_error("charset");}}
 ~DB(){mysql_close(c);}
 // Prepared statements bind every external value as a string or SQL NULL.
 void run(string sql,vector<json> args={}){
  unique_ptr<MYSQL_STMT,decltype(&mysql_stmt_close)> st(mysql_stmt_init(c),mysql_stmt_close);
  if(!st||mysql_stmt_prepare(st.get(),sql.c_str(),static_cast<unsigned long>(sql.size())))throw runtime_error("prepare");
  vector<string> vals(args.size());vector<MYSQL_BIND> binds(args.size());vector<unsigned long> lengths(args.size());
  for(size_t i=0;i<args.size();i++){auto& b=binds[i];if(args[i].is_null()){b.buffer_type=MYSQL_TYPE_NULL;continue;}vals[i]=args[i].is_string()?args[i].get<string>():args[i].dump();lengths[i]=static_cast<unsigned long>(vals[i].size());b.buffer_type=MYSQL_TYPE_STRING;b.buffer=vals[i].data();b.buffer_length=lengths[i];b.length=&lengths[i];}
  if((!args.empty()&&mysql_stmt_bind_param(st.get(),binds.data()))||mysql_stmt_execute(st.get()))throw runtime_error("execute");
  affected=mysql_stmt_affected_rows(st.get());id=mysql_stmt_insert_id(st.get());
 }
 unsigned long long affected=0,id=0;
 json query(string sql){if(mysql_query(c,sql.c_str()))throw runtime_error("query");unique_ptr<MYSQL_RES,decltype(&mysql_free_result)> r(mysql_store_result(c),mysql_free_result);if(!r)throw runtime_error("result");json a=json::array();auto fields=mysql_fetch_fields(r.get());auto n=mysql_num_fields(r.get());while(auto row=mysql_fetch_row(r.get())){json j;for(unsigned i=0;i<n;i++)j[fields[i].name]=row[i]?json(row[i]):json();a.push_back(j);}return a;}
};
struct Session{string id;Clock::time_point expiry;};
mutex mu;map<string,Session> sessions;map<string,pair<Clock::time_point,int>> attempts;
void reply(httplib::Response& r,int status,json j){r.status=status;r.set_content(j.dump(),"application/json");}
json auth(const httplib::Request& q){auto h=q.get_header_value("Authorization");if(h.rfind("Bearer ",0)!=0)return nullptr;string id;{lock_guard<mutex> l(mu);auto i=sessions.find(h.substr(7));if(i==sessions.end()||i->second.expiry<Clock::now())return nullptr;id=i->second.id;}DB d;auto a=d.query("SELECT id,name,role FROM users WHERE id="+to_string(stoull(id)));return a.empty()?json():a[0];}
bool allowed(const httplib::Request&q,httplib::Response&r,bool admin=false){auto u=auth(q);if(u.is_null()){r.status=401;return false;}if(admin&&u["role"]!="admin"){r.status=403;return false;}return true;}
json body(const httplib::Request&q){return json::parse(q.body,nullptr,false);}
vector<json> values(json t){return {t["title"],t["description"],t["customer"],t["priority"],t["status"],t.value("assigned_to",json())};}
int main(int argc,char**argv){try{
 config();
 if(argc>1&&string(argv[1])=="--seed"){auto p=get("ADMIN_PASSWORD"),e=get("ADMIN_EMAIL");if(p.size()<12||p.size()>128||e.empty())throw runtime_error("Configure ADMIN_EMAIL e ADMIN_PASSWORD");DB d;d.run("INSERT INTO users(name,email,password_hash,role) VALUES(?,?,?,?)",{"Administrador",e,hashpw(p),"admin"});cout<<"Administrador criado\n";return 0;}
 httplib::Server s;s.set_payload_max_length(32768);
 s.set_default_headers({{"X-Content-Type-Options","nosniff"},{"X-Frame-Options","DENY"},{"Cache-Control","no-store"},{"Content-Security-Policy","default-src 'self'; frame-ancestors 'none'"}});
 s.set_exception_handler([](const auto&,auto&r,exception_ptr){reply(r,500,{{"error","Falha na operacao"}});});
 s.set_mount_point("/","./public");
 s.Get("/",[](const auto&,auto&r){ifstream f("public/index.html");stringstream b;b<<f.rdbuf();r.set_content(b.str(),"text/html");});
 s.Get("/openapi.json",[](const auto&,auto&r){ifstream f("docs/openapi.json");stringstream b;b<<f.rdbuf();r.set_content(b.str(),"application/json");});
 s.Get("/docs",[](const auto&,auto&r){r.set_content("<html><body><h1>Help Desk Pro API</h1><a href='/openapi.json'>OpenAPI JSON</a></body></html>","text/html");});
 s.Post("/api/login",[](const httplib::Request&q,httplib::Response&r){
  {lock_guard<mutex> l(mu);auto& a=attempts[q.remote_addr];if(Clock::now()-a.first>chrono::minutes(15))a={Clock::now(),0};if(++a.second>20){r.status=429;return;}}
  auto t=body(q);if(!t.is_object()||!t.contains("email")||!t["email"].is_string()||!t.contains("password")||!t["password"].is_string()){r.status=400;return;}
  string p=t["password"];if(p.size()>128){r.status=400;return;}
  DB d;auto a=d.query("SELECT id,name,email,password_hash,role FROM users");for(auto&u:a){if(u["email"]==t["email"]&&checkpw(p,u["password_hash"])){auto token=random_hex(32);{lock_guard<mutex> l(mu);for(auto i=sessions.begin();i!=sessions.end();)if(i->second.expiry<Clock::now())i=sessions.erase(i);else ++i;sessions[token]={u["id"],Clock::now()+chrono::hours(2)};}reply(r,200,{{"token",token},{"name",u["name"]},{"role",u["role"]}});return;}}r.status=401;
 });
 s.Get("/api/users",[](const httplib::Request&q,httplib::Response&r){if(!allowed(q,r))return;DB d;reply(r,200,d.query("SELECT id,name,role FROM users ORDER BY name"));});
 s.Post("/api/users",[](const httplib::Request&q,httplib::Response&r){if(!allowed(q,r,true))return;auto t=body(q);if(!t.is_object()){r.status=400;return;}for(auto k:{"name","email","password"})if(!t.contains(k)||!t[k].is_string()){r.status=400;return;}string p=t["password"],n=t["name"],e=t["email"];if(p.size()<12||p.size()>128||n.empty()||n.size()>120||e.size()>190||e.find('@')==string::npos){r.status=400;return;}DB d;d.run("INSERT INTO users(name,email,password_hash) VALUES(?,?,?)",{n,e,hashpw(p)});reply(r,201,{{"id",d.id}});});
 s.Get("/api/tickets",[](const httplib::Request&q,httplib::Response&r){if(!allowed(q,r))return;DB d;auto a=d.query("SELECT t.*,u.name AS technician FROM tickets t LEFT JOIN users u ON u.id=t.assigned_to ORDER BY t.id DESC");for(auto&t:a){t["id"]=stoll(t["id"].get<string>());if(!t["assigned_to"].is_null())t["assigned_to"]=stoll(t["assigned_to"].get<string>());}reply(r,200,a);});
 s.Post("/api/tickets",[](const httplib::Request&q,httplib::Response&r){if(!allowed(q,r))return;auto t=body(q);if(!valid_ticket(t)){r.status=400;return;}DB d;d.run("INSERT INTO tickets(title,description,customer,priority,status,assigned_to) VALUES(?,?,?,?,?,?)",values(t));reply(r,201,{{"id",d.id}});});
 s.Put(R"(/api/tickets/(\d+))",[](const httplib::Request&q,httplib::Response&r){if(!allowed(q,r))return;auto t=body(q);if(!valid_ticket(t)){r.status=400;return;}DB d;auto id=to_string(stoull(q.matches[1].str()));if(d.query("SELECT id FROM tickets WHERE id="+id).empty()){r.status=404;return;}auto v=values(t);v.push_back(id);d.run("UPDATE tickets SET title=?,description=?,customer=?,priority=?,status=?,assigned_to=? WHERE id=?",v);r.status=204;});
 s.Delete(R"(/api/tickets/(\d+))",[](const httplib::Request&q,httplib::Response&r){if(!allowed(q,r,true))return;DB d;d.run("DELETE FROM tickets WHERE id=?",{q.matches[1].str()});r.status=d.affected?204:404;});
 cout<<"http://localhost:"<<get("PORT","8080")<<endl;return s.listen("127.0.0.1",stoi(get("PORT","8080")))?0:1;
 }catch(const exception&e){cerr<<e.what()<<endl;return 1;}}
