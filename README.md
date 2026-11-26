# Help Desk Pro — C++ Edition

Aplicação full-stack em C++17, cpp-httplib, MySQL, HTML, CSS e JavaScript. Dashboard, login, CRUD de chamados, técnicos, permissões e filtros.

## Windows / VS Code
Instale Git, CMake, Visual Studio Build Tools com desenvolvimento desktop C++ e vcpkg. No PowerShell, configure vcpkg:
```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -A x64
cmake --build build --config Release
```
Execute a partir da raiz do projeto. Inicie MySQL no XAMPP, importe `database/schema.sql` no phpMyAdmin e copie `.env.example` para `.env`. Configure credenciais e senha administrativa de 12 a 128 bytes.
```powershell
Copy-Item .env.example .env
# Edite .env antes de prosseguir
.\build\Release\helpdesk.exe --seed
.\build\Release\helpdesk.exe
```
Abra http://localhost:8080. Documentação OpenAPI: http://localhost:8080/docs.
O banco `helpdesk_cpp` é independente das versões anteriores.

## Testes
`ctest --test-dir build -C Release --output-on-failure`

## GitHub
Crie um repositório vazio `helpdesk-pro-cpp`.
```bash
git init
git add .
git commit -m "feat: initial C++ Help Desk Pro"
git branch -M main
git remote add origin https://github.com/SEU_USUARIO/helpdesk-pro-cpp.git
git push -u origin main
```

## Segurança e limitações
Tokens opacos aleatórios com expiração de duas horas, sessões em memória e PBKDF2-HMAC-SHA256 com salt e 600 mil iterações (não JWT/bcrypt). Consultas de escrita preparadas; consultas de IDs usam números convertidos. Login limitado por IP em memória; a consulta de login percorre usuários, adequada apenas a demonstração pequena. Logout da interface descarta o token local, sem revogação no servidor. A documentação é OpenAPI JSON, sem Swagger UI incorporado. Clientes não têm login próprio; técnicos podem editar todos os chamados. Servidor local, sem HTTPS, sem SLA ou auditoria. Não publique .env. Dependências e compilação integral precisam ser verificadas no ambiente de destino.
