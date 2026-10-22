// ============================================================================
// SISTEMA DE AEROLINEA Y VUELOS - ENFOQUE EN CONTROL Y TICKETS DE EQUIPAJE
// Roles: 1) Pasajero  2) Ventanilla (Edicion y Check)  3) Supervisor de Vuelos
// Conectado a Base de Datos MySQL (MySQL Server 8.0) + Interfaz Grafica HTML
// Compilar: g++ -std=c++17 sistema_aerolinea.cpp -o sistema_aerolinea
// ============================================================================

#include <iostream>
#include <string>
#include <vector>
#include <limits>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;

// ============================================================================
// INTERFAZ DINAMICA PARA MYSQL C API (libmysql.dll)
// ============================================================================
struct MYSQL;
struct MYSQL_RES;
typedef char** MYSQL_ROW;
typedef unsigned long long my_ulonglong;

typedef MYSQL* (*fn_mysql_init)(MYSQL*);
typedef MYSQL* (*fn_mysql_real_connect)(MYSQL*, const char*, const char*, const char*, const char*, unsigned int, const char*, unsigned long);
typedef int (*fn_mysql_query)(MYSQL*, const char*);
typedef MYSQL_RES* (*fn_mysql_store_result)(MYSQL*);
typedef MYSQL_ROW (*fn_mysql_fetch_row)(MYSQL_RES*);
typedef void (*fn_mysql_free_result)(MYSQL_RES*);
typedef const char* (*fn_mysql_error)(MYSQL*);
typedef void (*fn_mysql_close)(MYSQL*);
typedef my_ulonglong (*fn_mysql_insert_id)(MYSQL*);
typedef int (*fn_mysql_set_character_set)(MYSQL*, const char*);

struct MySQLDriver {
    HMODULE hDll = nullptr;
    fn_mysql_init init = nullptr;
    fn_mysql_real_connect real_connect = nullptr;
    fn_mysql_query query = nullptr;
    fn_mysql_store_result store_result = nullptr;
    fn_mysql_fetch_row fetch_row = nullptr;
    fn_mysql_free_result free_result = nullptr;
    fn_mysql_error error = nullptr;
    fn_mysql_close close = nullptr;
    fn_mysql_insert_id insert_id = nullptr;
    fn_mysql_set_character_set set_charset = nullptr;
    MYSQL* conn = nullptr;

    bool cargarDLL() {
#ifdef _WIN32
        const char* rutasLib[] = {
            "C:\\Program Files\\MySQL\\MySQL Server 8.0\\lib",
            "C:\\Program Files\\MySQL\\MySQL Server 8.0\\bin",
            "C:\\xampp\\mysql\\lib",
            nullptr
        };
        for (int i = 0; rutasLib[i] != nullptr; ++i) {
            SetDllDirectoryA(rutasLib[i]);
            string dllPath = string(rutasLib[i]) + "\\libmysql.dll";
            hDll = LoadLibraryA(dllPath.c_str());
            if (hDll) break;
        }
        if (!hDll) hDll = LoadLibraryA("libmysql.dll");
        if (!hDll) return false;

        init = reinterpret_cast<fn_mysql_init>(reinterpret_cast<void*>(GetProcAddress(hDll, "mysql_init")));
        real_connect = reinterpret_cast<fn_mysql_real_connect>(reinterpret_cast<void*>(GetProcAddress(hDll, "mysql_real_connect")));
        query = reinterpret_cast<fn_mysql_query>(reinterpret_cast<void*>(GetProcAddress(hDll, "mysql_query")));
        store_result = reinterpret_cast<fn_mysql_store_result>(reinterpret_cast<void*>(GetProcAddress(hDll, "mysql_store_result")));
        fetch_row = reinterpret_cast<fn_mysql_fetch_row>(reinterpret_cast<void*>(GetProcAddress(hDll, "mysql_fetch_row")));
        free_result = reinterpret_cast<fn_mysql_free_result>(reinterpret_cast<void*>(GetProcAddress(hDll, "mysql_free_result")));
        error = reinterpret_cast<fn_mysql_error>(reinterpret_cast<void*>(GetProcAddress(hDll, "mysql_error")));
        close = reinterpret_cast<fn_mysql_close>(reinterpret_cast<void*>(GetProcAddress(hDll, "mysql_close")));
        insert_id = reinterpret_cast<fn_mysql_insert_id>(reinterpret_cast<void*>(GetProcAddress(hDll, "mysql_insert_id")));
        set_charset = reinterpret_cast<fn_mysql_set_character_set>(reinterpret_cast<void*>(GetProcAddress(hDll, "mysql_set_character_set")));

        return init && real_connect && query && store_result && fetch_row &&
               free_result && error && close && insert_id;
#else
        return false;
#endif
    }

    ~MySQLDriver() {
        if (conn && close) close(conn);
        if (hDll) FreeLibrary(hDll);
    }
} db;

// ============================================================================
// POLITICAS Y TARIFAS DE EQUIPAJE
// ============================================================================
const int MAX_MANO_GRATIS = 1;           // Hasta 1 maleta de mano sin cobro extra
const int MAX_GRANDE_GRATIS = 2;         // Hasta 2 maletas grandes sin cobro extra
const double COSTO_EXTRA_MANO = 150.00;  // Q150.00 por cada maleta de mano adicional (>1)
const double COSTO_EXTRA_GRANDE = 300.00;// Q300.00 por cada maleta grande adicional (>2)

enum Estado { EN_ESPERA, CONFIRMADO, CANCELADO };

struct Pasajero {
    string nombre, fechaNacimiento, nacionalidad, documento, vencimientoPasaporte;
    int edad = 0;
};

struct Vuelo {
    string origen, destino, fechaSalida, fechaRegreso, horario, clase;
    string preferenciaDirecto, aerolinea;
    int cupoMaximo = 0;
    double presupuesto = 0.0;
};

struct Preferencias {
    string equipajeMano, equipajeDocumentado, asiento;
};

struct Contacto {
    string telefono, correo, paisResidencia, emergencia;
};

struct TicketEquipaje {
    int id = 0;
    string codigoTicket;
    int reservaId = 0;
    string tipoMaleta; // "DE MANO" o "GRANDE"
    int numeroPieza = 0;
    bool esExtra = false;
    double costo = 0.0;
    string fechaRegistro;
};

struct Reserva {
    int id = 0;
    Pasajero pasajero;
    Vuelo vuelo;
    Preferencias preferencias;
    Contacto contacto;
    int cantidadPasajeros = 1;
    int equipajeManoPiezas = 0;
    int equipajeDocumentadoPiezas = 0;
    double cobroExtraMano = 0.0;
    double cobroExtraGrande = 0.0;
    double cobroTotalEquipaje = 0.0;
    string metodoPago, requiereFactura, viajeroFrecuente;
    Estado estado = EN_ESPERA;
};

// ============================================================================
// FUNCIONES AUXILIARES
// ============================================================================
void limpiarEntrada() {
    cin.clear();
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
}

string leerTexto(const string& mensaje) {
    string valor;
    cout << mensaje;
    getline(cin, valor);
    while (valor.empty()) {
        cout << "Este campo es obligatorio. Intente de nuevo: ";
        getline(cin, valor);
    }
    return valor;
}

string leerTextoOpcional(const string& mensaje, const string& valorActual) {
    string valor;
    cout << mensaje << " [" << valorActual << "]: ";
    getline(cin, valor);
    return valor.empty() ? valorActual : valor;
}

int leerEntero(const string& mensaje, int minimo, int maximo) {
    int valor;
    while (true) {
        cout << mensaje;
        if (cin >> valor && valor >= minimo && valor <= maximo) {
            limpiarEntrada();
            return valor;
        }
        cout << "Ingrese un numero entre " << minimo << " y " << maximo << ".\n";
        limpiarEntrada();
    }
}

double leerDecimal(const string& mensaje, double minimo) {
    double valor;
    while (true) {
        cout << mensaje;
        if (cin >> valor && valor >= minimo) {
            limpiarEntrada();
            return valor;
        }
        cout << "Ingrese una cantidad valida.\n";
        limpiarEntrada();
    }
}

string estadoTexto(Estado estado) {
    switch (estado) {
        case EN_ESPERA: return "EN ESPERA";
        case CONFIRMADO: return "CONFIRMADO";
        case CANCELADO: return "CANCELADO";
    }
    return "DESCONOCIDO";
}

Estado textoAEstado(const string& s) {
    if (s == "CONFIRMADO") return CONFIRMADO;
    if (s == "CANCELADO") return CANCELADO;
    return EN_ESPERA;
}

string sqlEscape(const string& s) {
    string r;
    for (char c : s) {
        if (c == '\'') r += "''";
        else if (c == '\\') r += "\\\\";
        else r += c;
    }
    return r;
}

string formatearCodigoTicket(int idTicket, int idReserva, const string& tipo, int numPieza) {
    ostringstream oss;
    char prefijoTipo = (tipo == "DE MANO") ? 'M' : 'G';
    oss << "TK-" << setw(4) << setfill('0') << idTicket
        << "-R" << idReserva << "-" << prefijoTipo << numPieza;
    return oss.str();
}

bool ejecutarSQL(const string& sql) {
    if (db.query(db.conn, sql.c_str()) != 0) {
        cout << "[Error MySQL]: " << db.error(db.conn) << "\n";
        return false;
    }
    return true;
}

// ============================================================================
// GENERACION DE TICKETS POR CADA MALETA EN MYSQL
// ============================================================================
void regenerarTicketsEquipajeDB(int reservaId, int piezasMano, int piezasGrandes) {
    ostringstream del;
    del << "DELETE FROM tickets_equipaje WHERE reserva_id = " << reservaId << ";";
    ejecutarSQL(del.str());

    for (int i = 1; i <= piezasMano; ++i) {
        bool esExtra = (i > MAX_MANO_GRATIS);
        double costo = esExtra ? COSTO_EXTRA_MANO : 0.00;
        ostringstream ins;
        ins << "INSERT INTO tickets_equipaje (codigo_ticket, reserva_id, tipo_maleta, numero_pieza, es_extra, costo) "
            << "VALUES (NULL, " << reservaId << ", 'DE MANO', " << i << ", " << (esExtra ? 1 : 0) << ", "
            << fixed << setprecision(2) << costo << ");";
        if (ejecutarSQL(ins.str())) {
            int ticketId = static_cast<int>(db.insert_id(db.conn));
            string codigo = formatearCodigoTicket(ticketId, reservaId, "DE MANO", i);
            ostringstream upd;
            upd << "UPDATE tickets_equipaje SET codigo_ticket = '" << sqlEscape(codigo)
                << "' WHERE id = " << ticketId << ";";
            ejecutarSQL(upd.str());
        }
    }

    for (int i = 1; i <= piezasGrandes; ++i) {
        bool esExtra = (i > MAX_GRANDE_GRATIS);
        double costo = esExtra ? COSTO_EXTRA_GRANDE : 0.00;
        ostringstream ins;
        ins << "INSERT INTO tickets_equipaje (codigo_ticket, reserva_id, tipo_maleta, numero_pieza, es_extra, costo) "
            << "VALUES (NULL, " << reservaId << ", 'GRANDE', " << i << ", " << (esExtra ? 1 : 0) << ", "
            << fixed << setprecision(2) << costo << ");";
        if (ejecutarSQL(ins.str())) {
            int ticketId = static_cast<int>(db.insert_id(db.conn));
            string codigo = formatearCodigoTicket(ticketId, reservaId, "GRANDE", i);
            ostringstream upd;
            upd << "UPDATE tickets_equipaje SET codigo_ticket = '" << sqlEscape(codigo)
                << "' WHERE id = " << ticketId << ";";
            ejecutarSQL(upd.str());
        }
    }
}

int totalReservasRegistradas() {
    string sql = "SELECT COUNT(*) FROM reservas;";
    if (db.query(db.conn, sql.c_str()) == 0) {
        MYSQL_RES* res = db.store_result(db.conn);
        if (res) {
            MYSQL_ROW row = db.fetch_row(res);
            int total = (row && row[0]) ? atoi(row[0]) : 0;
            db.free_result(res);
            return total;
        }
    }
    return 0;
}

// ============================================================================
// CARGA AUTOMATICA DE 10 USUARIOS DE PRUEBA CON SUS VUELOS Y MALETAS EN MYSQL
// ============================================================================
void insertarPasajeroPrueba(const string& nombre, const string& fnac, int edad, const string& nac,
                            const string& doc, const string& venc, const string& origen,
                            const string& destino, const string& fsal, const string& horario,
                            const string& clase, const string& aerolinea, double presup,
                            const string& asiento, int mano, int grande, const string& tel,
                            const string& correo, const string& pago, const string& estado) {
    int extMano = max(0, mano - MAX_MANO_GRATIS);
    int extGrande = max(0, grande - MAX_GRANDE_GRATIS);
    double cMano = extMano * COSTO_EXTRA_MANO;
    double cGrande = extGrande * COSTO_EXTRA_GRANDE;
    double cTotal = cMano + cGrande;

    ostringstream sql;
    sql << "INSERT INTO reservas ("
        << "nombre, fecha_nacimiento, edad, nacionalidad, documento, vencimiento_pasaporte, "
        << "origen, destino, fecha_salida, fecha_regreso, horario, clase, preferencia_directo, aerolinea, "
        << "presupuesto, cantidad_pasajeros, asiento, equipaje_mano_piezas, equipaje_grande_piezas, "
        << "cobro_extra_mano, cobro_extra_grande, cobro_total_equipaje, telefono, correo, pais_residencia, "
        << "emergencia, metodo_pago, requiere_factura, viajero_frecuente, estado"
        << ") VALUES ("
        << "'" << sqlEscape(nombre) << "', '" << sqlEscape(fnac) << "', " << edad << ", "
        << "'" << sqlEscape(nac) << "', '" << sqlEscape(doc) << "', '" << sqlEscape(venc) << "', "
        << "'" << sqlEscape(origen) << "', '" << sqlEscape(destino) << "', '" << sqlEscape(fsal) << "', "
        << "'No aplica', '" << sqlEscape(horario) << "', '" << sqlEscape(clase) << "', 'Directo', "
        << "'" << sqlEscape(aerolinea) << "', " << fixed << setprecision(2) << presup << ", 1, "
        << "'" << sqlEscape(asiento) << "', " << mano << ", " << grande << ", "
        << cMano << ", " << cGrande << ", " << cTotal << ", "
        << "'" << sqlEscape(tel) << "', '" << sqlEscape(correo) << "', 'Guatemala', 'Familiar directo', "
        << "'" << sqlEscape(pago) << "', 'si', 'AG-PLUS', '" << sqlEscape(estado) << "');";

    if (ejecutarSQL(sql.str())) {
        int idReserva = static_cast<int>(db.insert_id(db.conn));
        regenerarTicketsEquipajeDB(idReserva, mano, grande);
    }
}

void sembrarDiezUsuariosDePruebaSiVacio() {
    if (totalReservasRegistradas() > 0) return;

    cout << "[MySQL] Cargando 10 pasajeros de prueba con sus vuelos y tickets de maletas...\n";
    insertarPasajeroPrueba("Cristian Josue Pacheco", "14/03/2001", 25, "Guatemalteca", "P-2732701", "15/09/2031",
                           "Ciudad de Guatemala", "Madrid", "25/10/2026", "Manana", "Ejecutiva", "AG-101 AeroGuate",
                           6800.00, "Ventana", 2, 3, "5512-3401", "cristian.pacheco@correo.gt", "Tarjeta de Credito", "CONFIRMADO");

    insertarPasajeroPrueba("Ana Sofia Morales Herrera", "08/07/1996", 30, "Guatemalteca", "P-8841209", "20/11/2029",
                           "Ciudad de Guatemala", "Madrid", "25/10/2026", "Manana", "Economica", "AG-101 AeroGuate",
                           4950.00, "Pasillo", 1, 2, "4489-1102", "anasofia.morales@gmail.com", "Tarjeta de Debito", "CONFIRMADO");

    insertarPasajeroPrueba("Roberto Carlos Estrada Gomez", "22/01/1984", 42, "Mexicana", "P-3391044", "10/05/2028",
                           "Ciudad de Guatemala", "Ciudad de Mexico", "27/10/2026", "Manana", "Ejecutiva", "AG-309 AeroGuate",
                           3200.00, "Filas delanteras", 3, 4, "5021-9980", "roberto.estrada@empresa.mx", "Tarjeta de Credito", "EN ESPERA");

    insertarPasajeroPrueba("Maria Fernanda Lopez Castillo", "19/09/1999", 27, "Guatemalteca", "P-7712098", "03/12/2030",
                           "Ciudad de Guatemala", "Miami", "26/10/2026", "Tarde", "Economica Premium", "AG-204 AeroGuate",
                           3600.00, "Ventana", 2, 2, "3310-4567", "mafer.lopez@outlook.com", "Tarjeta de Credito", "CONFIRMADO");

    insertarPasajeroPrueba("Jorge Alejandro Ramirez Pineda", "05/11/1979", 46, "Salvadorena", "P-5501923", "18/04/2029",
                           "Ciudad de Guatemala", "Miami", "26/10/2026", "Tarde", "Primera Clase", "AG-204 AeroGuate",
                           7400.00, "Filas delanteras", 1, 4, "5890-2211", "jramirez@corporacion.sv", "Tarjeta de Credito", "EN ESPERA");

    insertarPasajeroPrueba("Lucia Gabriela Alvarado Ruiz", "12/02/2002", 24, "Guatemalteca", "P-9182736", "29/07/2031",
                           "Ciudad de Guatemala", "Flores, Peten", "28/10/2026", "Noche", "Economica", "AG-415 AeroGuate",
                           1150.00, "Ventana", 1, 1, "4123-7890", "lucia.alvarado@universidad.edu.gt", "Efectivo", "CONFIRMADO");

    insertarPasajeroPrueba("Diego Fernando Cabrera Soto", "30/06/1991", 35, "Colombiana", "P-6645120", "11/10/2028",
                           "Ciudad de Guatemala", "Madrid", "25/10/2026", "Manana", "Economica Premium", "AG-101 AeroGuate",
                           5600.00, "Salida de emergencia", 2, 3, "5901-3344", "diego.cabrera@bogota.co", "Tarjeta de Credito", "EN ESPERA");

    insertarPasajeroPrueba("Valeria Alejandra Quan Perez", "15/05/1997", 29, "Guatemalteca", "P-1190432", "07/01/2032",
                           "Ciudad de Guatemala", "Ciudad de Mexico", "27/10/2026", "Manana", "Economica", "AG-309 AeroGuate",
                           2100.00, "Pasillo", 2, 1, "4778-9012", "valeria.quan@design.gt", "Transferencia", "CONFIRMADO");

    insertarPasajeroPrueba("Hector Leonel Villatoro Cano", "03/12/1988", 37, "Guatemalteca", "P-4098112", "22/08/2029",
                           "Ciudad de Guatemala", "Flores, Peten", "28/10/2026", "Noche", "Economica", "AG-415 AeroGuate",
                           1250.00, "Ventana", 1, 3, "5234-6789", "hector.villatoro@agro.gt", "Efectivo", "EN ESPERA");

    insertarPasajeroPrueba("Paola Beatriz Santizo Marroquin", "27/08/1993", 33, "Guatemalteca", "P-8374651", "14/06/2030",
                           "Ciudad de Guatemala", "Miami", "26/10/2026", "Tarde", "Ejecutiva", "AG-204 AeroGuate",
                           4800.00, "Ventana", 3, 3, "3987-6543", "paola.santizo@medicos.gt", "Tarjeta de Credito", "CONFIRMADO");

    cout << "[OK] 10 usuarios de prueba y sus tickets de maletas han sido creados en MySQL.\n";
}

bool conectarEInicializarMySQL() {
    if (!db.cargarDLL()) {
        cout << "ERROR CRITICO: No se pudo cargar libmysql.dll.\n";
        return false;
    }

    cout << "=========================================================\n";
    cout << "        CONEXION A BASE DE DATOS MYSQL SERVER 8.0        \n";
    cout << "=========================================================\n";

    string host = "localhost";
    unsigned int port = 3306;
    string user = "root";
    string pass = "";

    vector<string> intentos = {"", "root", "1234", "123456", "admin"};
    bool conectado = false;

    for (const string& p : intentos) {
        db.conn = db.init(nullptr);
        if (db.real_connect(db.conn, host.c_str(), user.c_str(), p.c_str(), nullptr, port, nullptr, 0)) {
            conectado = true;
            pass = p;
            break;
        }
        db.close(db.conn);
        db.conn = nullptr;
    }

    while (!conectado) {
        cout << "Usuario MySQL (Enter para 'root'): ";
        string uInput;
        getline(cin, uInput);
        if (!uInput.empty()) user = uInput;

        cout << "Contrasena de MySQL para '" << user << "'@'localhost': ";
        getline(cin, pass);

        db.conn = db.init(nullptr);
        if (db.real_connect(db.conn, host.c_str(), user.c_str(), pass.c_str(), nullptr, port, nullptr, 0)) {
            conectado = true;
            break;
        } else {
            cout << "No se pudo conectar: " << db.error(db.conn) << "\nIntente nuevamente.\n\n";
            db.close(db.conn);
            db.conn = nullptr;
        }
    }

    if (db.set_charset) db.set_charset(db.conn, "utf8mb4");

    ejecutarSQL("CREATE DATABASE IF NOT EXISTS sistema_aerolinea CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;");
    ejecutarSQL("USE sistema_aerolinea;");

    ejecutarSQL(
        "CREATE TABLE IF NOT EXISTS reservas ("
        "id INT AUTO_INCREMENT PRIMARY KEY,"
        "nombre VARCHAR(150) NOT NULL,"
        "fecha_nacimiento VARCHAR(30),"
        "edad INT,"
        "nacionalidad VARCHAR(80),"
        "documento VARCHAR(60),"
        "vencimiento_pasaporte VARCHAR(30),"
        "origen VARCHAR(100),"
        "destino VARCHAR(100),"
        "fecha_salida VARCHAR(30),"
        "fecha_regreso VARCHAR(30),"
        "horario VARCHAR(50),"
        "clase VARCHAR(50),"
        "preferencia_directo VARCHAR(50),"
        "aerolinea VARCHAR(100),"
        "presupuesto DECIMAL(10,2),"
        "cantidad_pasajeros INT,"
        "asiento VARCHAR(60),"
        "equipaje_mano_piezas INT DEFAULT 0,"
        "equipaje_grande_piezas INT DEFAULT 0,"
        "cobro_extra_mano DECIMAL(10,2) DEFAULT 0.00,"
        "cobro_extra_grande DECIMAL(10,2) DEFAULT 0.00,"
        "cobro_total_equipaje DECIMAL(10,2) DEFAULT 0.00,"
        "telefono VARCHAR(40),"
        "correo VARCHAR(120),"
        "pais_residencia VARCHAR(80),"
        "emergencia VARCHAR(150),"
        "metodo_pago VARCHAR(60),"
        "requiere_factura VARCHAR(10),"
        "viajero_frecuente VARCHAR(80),"
        "estado VARCHAR(20)"
        ");"
    );

    ejecutarSQL(
        "CREATE TABLE IF NOT EXISTS tickets_equipaje ("
        "id INT AUTO_INCREMENT PRIMARY KEY,"
        "codigo_ticket VARCHAR(30) UNIQUE,"
        "reserva_id INT NOT NULL,"
        "tipo_maleta VARCHAR(30) NOT NULL,"
        "numero_pieza INT NOT NULL,"
        "es_extra TINYINT(1) DEFAULT 0,"
        "costo DECIMAL(10,2) DEFAULT 0.00,"
        "fecha_registro TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY (reserva_id) REFERENCES reservas(id) ON DELETE CASCADE"
        ");"
    );

    sembrarDiezUsuariosDePruebaSiVacio();
    cout << "[OK] Conectado a MySQL Server 8.0 | Base de datos: 'sistema_aerolinea' lista.\n";
    return true;
}

Reserva filaAReserva(MYSQL_ROW row) {
    Reserva r;
    r.id = row[0] ? atoi(row[0]) : 0;
    r.pasajero.nombre = row[1] ? row[1] : "";
    r.pasajero.fechaNacimiento = row[2] ? row[2] : "";
    r.pasajero.edad = row[3] ? atoi(row[3]) : 0;
    r.pasajero.nacionalidad = row[4] ? row[4] : "";
    r.pasajero.documento = row[5] ? row[5] : "";
    r.pasajero.vencimientoPasaporte = row[6] ? row[6] : "";
    r.vuelo.origen = row[7] ? row[7] : "";
    r.vuelo.destino = row[8] ? row[8] : "";
    r.vuelo.fechaSalida = row[9] ? row[9] : "";
    r.vuelo.fechaRegreso = row[10] ? row[10] : "";
    r.vuelo.horario = row[11] ? row[11] : "";
    r.vuelo.clase = row[12] ? row[12] : "";
    r.vuelo.preferenciaDirecto = row[13] ? row[13] : "";
    r.vuelo.aerolinea = row[14] ? row[14] : "";
    r.vuelo.presupuesto = row[15] ? atof(row[15]) : 0.0;
    r.cantidadPasajeros = row[16] ? atoi(row[16]) : 1;
    r.preferencias.asiento = row[17] ? row[17] : "";
    r.equipajeManoPiezas = row[18] ? atoi(row[18]) : 0;
    r.equipajeDocumentadoPiezas = row[19] ? atoi(row[19]) : 0;
    r.cobroExtraMano = row[20] ? atof(row[20]) : 0.0;
    r.cobroExtraGrande = row[21] ? atof(row[21]) : 0.0;
    r.cobroTotalEquipaje = row[22] ? atof(row[22]) : 0.0;
    r.contacto.telefono = row[23] ? row[23] : "";
    r.contacto.correo = row[24] ? row[24] : "";
    r.contacto.paisResidencia = row[25] ? row[25] : "";
    r.contacto.emergencia = row[26] ? row[26] : "";
    r.metodoPago = row[27] ? row[27] : "";
    r.requiereFactura = row[28] ? row[28] : "";
    r.viajeroFrecuente = row[29] ? row[29] : "";
    r.estado = textoAEstado(row[30] ? row[30] : "EN ESPERA");
    return r;
}

bool obtenerReservaPorId(int id, Reserva& rOut) {
    ostringstream sql;
    sql << "SELECT id, nombre, fecha_nacimiento, edad, nacionalidad, documento, vencimiento_pasaporte, "
        << "origen, destino, fecha_salida, fecha_regreso, horario, clase, preferencia_directo, aerolinea, "
        << "presupuesto, cantidad_pasajeros, asiento, equipaje_mano_piezas, equipaje_grande_piezas, "
        << "cobro_extra_mano, cobro_extra_grande, cobro_total_equipaje, telefono, correo, pais_residencia, "
        << "emergencia, metodo_pago, requiere_factura, viajero_frecuente, estado "
        << "FROM reservas WHERE id = " << id << ";";

    if (db.query(db.conn, sql.str().c_str()) != 0) return false;
    MYSQL_RES* res = db.store_result(db.conn);
    if (!res) return false;
    MYSQL_ROW row = db.fetch_row(res);
    bool encontrado = false;
    if (row) {
        rOut = filaAReserva(row);
        encontrado = true;
    }
    db.free_result(res);
    return encontrado;
}

void mostrarTicketsDePasajero(int reservaId) {
    ostringstream sql;
    sql << "SELECT id, codigo_ticket, tipo_maleta, numero_pieza, es_extra, costo "
        << "FROM tickets_equipaje WHERE reserva_id = " << reservaId << " ORDER BY id ASC;";
    if (db.query(db.conn, sql.str().c_str()) != 0) return;
    MYSQL_RES* res = db.store_result(db.conn);
    if (!res) return;

    vector<TicketEquipaje> tickets;
    MYSQL_ROW row;
    while ((row = db.fetch_row(res))) {
        TicketEquipaje t;
        t.id = row[0] ? atoi(row[0]) : 0;
        t.codigoTicket = row[1] ? row[1] : "";
        t.tipoMaleta = row[2] ? row[2] : "";
        t.numeroPieza = row[3] ? atoi(row[3]) : 0;
        t.esExtra = (row[4] && atoi(row[4]) != 0);
        t.costo = row[5] ? atof(row[5]) : 0.0;
        tickets.push_back(t);
    }
    db.free_result(res);

    cout << "\n   --- TICKETS DE EQUIPAJE GENERADOS (" << tickets.size() << " maleta(s)) ---\n";
    if (tickets.empty()) {
        cout << "   El pasajero no lleva maletas registradas.\n";
        return;
    }
    cout << "   " << left << setw(20) << "CODIGO TICKET"
         << setw(14) << "TIPO MALETA"
         << setw(10) << "PIEZA #"
         << setw(22) << "CONDICION"
         << "COSTO\n";
    cout << "   ------------------------------------------------------------------------\n";
    for (const auto& t : tickets) {
        cout << "   " << left << setw(20) << t.codigoTicket
             << setw(14) << t.tipoMaleta
             << setw(10) << ("#" + to_string(t.numeroPieza))
             << setw(22) << (t.esExtra ? "EXTRA (Con cobro)" : "Incluida (Sin costo)")
             << "Q" << fixed << setprecision(2) << t.costo << "\n";
    }
}

void mostrarReserva(const Reserva& r, bool incluirTickets = true) {
    int totalMaletas = r.equipajeManoPiezas + r.equipajeDocumentadoPiezas;
    cout << "\n========================================================================\n";
    cout << "Reserva #" << r.id << " | Check Ventanilla: "
         << (r.estado == CONFIRMADO ? "[✓] CONFIRMADO" : "[ ] PENDIENTE DE CHECK") << '\n';
    cout << "------------------------------------------------------------------------\n";
    cout << "  Pasajero:   " << r.pasajero.nombre << " | Doc: " << r.pasajero.documento
         << " | Nac: " << r.pasajero.nacionalidad << " | Edad: " << r.pasajero.edad << '\n';
    cout << "  Contacto:   Tel: " << r.contacto.telefono << " | Correo: " << r.contacto.correo << '\n';
    cout << "  Vuelo:      " << r.vuelo.aerolinea << " | Ruta: " << r.vuelo.origen << " -> " << r.vuelo.destino
         << " | Salida: " << r.vuelo.fechaSalida << " (" << r.vuelo.horario << ")\n";
    cout << "  Clase:      " << r.vuelo.clase << " | Asiento: " << r.preferencias.asiento << '\n';
    cout << "  Equipaje:   " << totalMaletas << " maleta(s) en total ("
         << r.equipajeManoPiezas << " de mano, " << r.equipajeDocumentadoPiezas << " grandes)\n";
    cout << "  Cobro extra equipaje: Q" << fixed << setprecision(2) << r.cobroTotalEquipaje
         << " (Mano extra: Q" << r.cobroExtraMano << " | Grande extra: Q" << r.cobroExtraGrande << ")\n";
    if (incluirTickets) mostrarTicketsDePasajero(r.id);
    cout << "========================================================================\n";
}

// ============================================================================
// ROL 1: PASAJERO (REGISTRAR DATOS Y SELECCIONAR MALETAS)
// ============================================================================
void rolPasajeroRegistrar() {
    Reserva r;
    cout << "\n=== [ROL PASAJERO] REGISTRO DE VUELO Y SELECCION DE EQUIPAJE ===\n";
    r.pasajero.nombre = leerTexto("Nombre completo: ");
    r.pasajero.documento = leerTexto("Numero de pasaporte/documento: ");
    r.pasajero.vencimientoPasaporte = leerTexto("Vencimiento del documento (DD/MM/AAAA): ");
    r.pasajero.fechaNacimiento = leerTexto("Fecha de nacimiento (DD/MM/AAAA): ");
    r.pasajero.edad = leerEntero("Edad: ", 0, 120);
    r.pasajero.nacionalidad = leerTexto("Nacionalidad: ");
    r.contacto.telefono = leerTexto("Telefono: ");
    r.contacto.correo = leerTexto("Correo electronico: ");

    cout << "\nSeleccione el Vuelo:\n";
    cout << "  1) AG-101 | Ciudad de Guatemala -> Madrid (25/10/2026 - Manana)\n";
    cout << "  2) AG-204 | Ciudad de Guatemala -> Miami (26/10/2026 - Tarde)\n";
    cout << "  3) AG-309 | Ciudad de Guatemala -> Ciudad de Mexico (27/10/2026 - Manana)\n";
    cout << "  4) AG-415 | Ciudad de Guatemala -> Flores, Peten (28/10/2026 - Noche)\n";
    int opVuelo = leerEntero("Opcion de vuelo (1-4): ", 1, 4);
    r.vuelo.origen = "Ciudad de Guatemala";
    if (opVuelo == 1) { r.vuelo.aerolinea = "AG-101 AeroGuate"; r.vuelo.destino = "Madrid"; r.vuelo.fechaSalida = "25/10/2026"; r.vuelo.horario = "Manana"; }
    else if (opVuelo == 2) { r.vuelo.aerolinea = "AG-204 AeroGuate"; r.vuelo.destino = "Miami"; r.vuelo.fechaSalida = "26/10/2026"; r.vuelo.horario = "Tarde"; }
    else if (opVuelo == 3) { r.vuelo.aerolinea = "AG-309 AeroGuate"; r.vuelo.destino = "Ciudad de Mexico"; r.vuelo.fechaSalida = "27/10/2026"; r.vuelo.horario = "Manana"; }
    else { r.vuelo.aerolinea = "AG-415 AeroGuate"; r.vuelo.destino = "Flores, Peten"; r.vuelo.fechaSalida = "28/10/2026"; r.vuelo.horario = "Noche"; }

    r.vuelo.clase = leerTexto("Clase (Economica / Ejecutiva / Primera Clase): ");
    r.preferencias.asiento = leerTexto("Asiento preferido (Ventana / Pasillo): ");
    r.vuelo.presupuesto = leerDecimal("Costo base del boleto (Q): ", 0);
    r.metodoPago = leerTexto("Metodo de pago: ");

    cout << "\n--- SELECCION DE EQUIPAJE ---\n";
    cout << "(1 maleta de mano gratis, extra Q150 | 2 maletas grandes gratis, extra Q300)\n";
    r.equipajeManoPiezas = leerEntero("¿Cuantas maletas DE MANO lleva? (0-10): ", 0, 10);
    r.equipajeDocumentadoPiezas = leerEntero("¿Cuantas maletas GRANDES lleva? (0-10): ", 0, 10);

    insertarPasajeroPrueba(r.pasajero.nombre, r.pasajero.fechaNacimiento, r.pasajero.edad,
                           r.pasajero.nacionalidad, r.pasajero.documento, r.pasajero.vencimientoPasaporte,
                           r.vuelo.origen, r.vuelo.destino, r.vuelo.fechaSalida, r.vuelo.horario,
                           r.vuelo.clase, r.vuelo.aerolinea, r.vuelo.presupuesto, r.preferencias.asiento,
                           r.equipajeManoPiezas, r.equipajeDocumentadoPiezas, r.contacto.telefono,
                           r.contacto.correo, r.metodoPago, "EN ESPERA");

    int nuevoId = static_cast<int>(db.insert_id(db.conn));
    cout << "\n[OK] Pasajero registrado exitosamente. Sus datos pasaron a Ventanilla para verificacion.\n";
    Reserva guardada;
    if (obtenerReservaPorId(nuevoId, guardada)) {
        mostrarReserva(guardada, true);
    }
}

// ============================================================================
// ROL 2: AGENTE DE VENTANILLA (CORREGIR DATOS Y CONFIRMAR CON CHECK)
// ============================================================================
void rolVentanillaCorregirYConfirmar() {
    cout << "\n=== [ROL VENTANILLA] REVISION, CORRECCION DE DATOS Y CHECK DE CONFIRMACION ===\n";
    int id = leerEntero("Ingrese el ID del pasajero a revisar en ventanilla: ", 1, 1000000);
    Reserva r;
    if (!obtenerReservaPorId(id, r)) {
        cout << "No se encontro el pasajero #" << id << ".\n";
        return;
    }

    mostrarReserva(r, true);
    cout << "\n--- CORREGIR INFORMACION MAL INGRESADA (Presione Enter para conservar el valor actual) ---\n";
    r.pasajero.nombre = leerTextoOpcional("Nombre completo", r.pasajero.nombre);
    r.pasajero.documento = leerTextoOpcional("Documento/Pasaporte", r.pasajero.documento);
    r.pasajero.nacionalidad = leerTextoOpcional("Nacionalidad", r.pasajero.nacionalidad);
    r.contacto.telefono = leerTextoOpcional("Telefono", r.contacto.telefono);
    r.contacto.correo = leerTextoOpcional("Correo", r.contacto.correo);
    r.preferencias.asiento = leerTextoOpcional("Asiento", r.preferencias.asiento);

    string modEq = leerTextoOpcional("¿Desea corregir la cantidad de maletas? (si/no)", "no");
    if (modEq == "si" || modEq == "SI" || modEq == "Si") {
        r.equipajeManoPiezas = leerEntero("Nueva cantidad de maletas DE MANO (0-10): ", 0, 10);
        r.equipajeDocumentadoPiezas = leerEntero("Nueva cantidad de maletas GRANDES (0-10): ", 0, 10);
    }

    int extMano = max(0, r.equipajeManoPiezas - MAX_MANO_GRATIS);
    int extGrande = max(0, r.equipajeDocumentadoPiezas - MAX_GRANDE_GRATIS);
    r.cobroExtraMano = extMano * COSTO_EXTRA_MANO;
    r.cobroExtraGrande = extGrande * COSTO_EXTRA_GRANDE;
    r.cobroTotalEquipaje = r.cobroExtraMano + r.cobroExtraGrande;

    string check = leerTextoOpcional("¿Marcar CHECK [✓] de informacion confirmada en ventanilla? (si/no)", "si");
    if (check == "si" || check == "SI" || check == "Si") {
        r.estado = CONFIRMADO;
    } else {
        r.estado = EN_ESPERA;
    }

    ostringstream upd;
    upd << "UPDATE reservas SET "
        << "nombre = '" << sqlEscape(r.pasajero.nombre) << "', "
        << "documento = '" << sqlEscape(r.pasajero.documento) << "', "
        << "nacionalidad = '" << sqlEscape(r.pasajero.nacionalidad) << "', "
        << "telefono = '" << sqlEscape(r.contacto.telefono) << "', "
        << "correo = '" << sqlEscape(r.contacto.correo) << "', "
        << "asiento = '" << sqlEscape(r.preferencias.asiento) << "', "
        << "equipaje_mano_piezas = " << r.equipajeManoPiezas << ", "
        << "equipaje_grande_piezas = " << r.equipajeDocumentadoPiezas << ", "
        << "cobro_extra_mano = " << fixed << setprecision(2) << r.cobroExtraMano << ", "
        << "cobro_extra_grande = " << r.cobroExtraGrande << ", "
        << "cobro_total_equipaje = " << r.cobroTotalEquipaje << ", "
        << "estado = '" << estadoTexto(r.estado) << "' "
        << "WHERE id = " << r.id << ";";

    if (ejecutarSQL(upd.str())) {
        regenerarTicketsEquipajeDB(r.id, r.equipajeManoPiezas, r.equipajeDocumentadoPiezas);
        cout << "\n[✓] Datos actualizados y verificados en ventanilla correctamente:\n";
        obtenerReservaPorId(r.id, r);
        mostrarReserva(r, true);
    }
}

// ============================================================================
// ROL 3: SUPERVISOR (BUSCAR POR VUELO, PASAJERO Y TICKETS DE MALETAS)
// ============================================================================
void rolSupervisorBuscarPorVuelo() {
    cout << "\n=== [ROL SUPERVISOR] BUSCAR PASAJEROS Y MALETAS POR VUELO ===\n";
    cout << "  1) AG-101 | Ciudad de Guatemala -> Madrid\n";
    cout << "  2) AG-204 | Ciudad de Guatemala -> Miami\n";
    cout << "  3) AG-309 | Ciudad de Guatemala -> Ciudad de Mexico\n";
    cout << "  4) AG-415 | Ciudad de Guatemala -> Flores, Peten\n";
    cout << "  5) Mostrar TODOS los vuelos\n";
    int op = leerEntero("Seleccione el vuelo a inspeccionar (1-5): ", 1, 5);

    string filtroDestino = "";
    if (op == 1) filtroDestino = "Madrid";
    else if (op == 2) filtroDestino = "Miami";
    else if (op == 3) filtroDestino = "Ciudad de Mexico";
    else if (op == 4) filtroDestino = "Flores, Peten";

    ostringstream sql;
    sql << "SELECT id, nombre, fecha_nacimiento, edad, nacionalidad, documento, vencimiento_pasaporte, "
        << "origen, destino, fecha_salida, fecha_regreso, horario, clase, preferencia_directo, aerolinea, "
        << "presupuesto, cantidad_pasajeros, asiento, equipaje_mano_piezas, equipaje_grande_piezas, "
        << "cobro_extra_mano, cobro_extra_grande, cobro_total_equipaje, telefono, correo, pais_residencia, "
        << "emergencia, metodo_pago, requiere_factura, viajero_frecuente, estado "
        << "FROM reservas ";
    if (!filtroDestino.empty()) {
        sql << "WHERE destino = '" << sqlEscape(filtroDestino) << "' ";
    }
    sql << "ORDER BY id ASC;";

    if (db.query(db.conn, sql.str().c_str()) != 0) return;
    MYSQL_RES* res = db.store_result(db.conn);
    if (!res) return;

    vector<Reserva> lista;
    MYSQL_ROW row;
    while ((row = db.fetch_row(res))) {
        lista.push_back(filaAReserva(row));
    }
    db.free_result(res);

    if (lista.empty()) {
        cout << "No hay pasajeros registrados en este vuelo.\n";
        return;
    }

    for (const auto& r : lista) {
        mostrarReserva(r, true);
    }
}

void buscarPorTicketEquipaje() {
    cout << "\n=== [SUPERVISOR] BUSCAR TICKET DE EQUIPAJE ===\n";
    string criterio = leerTexto("Ingrese el codigo del ticket (ej. TK-0001-R1-M1) o numero de ticket: ");
    bool soloDigitos = !criterio.empty() && all_of(criterio.begin(), criterio.end(), ::isdigit);

    ostringstream sql;
    sql << "SELECT t.id, t.codigo_ticket, t.tipo_maleta, t.numero_pieza, t.es_extra, t.costo, t.fecha_registro, "
        << "r.id, r.nombre, r.documento, r.nacionalidad, r.edad, r.telefono, r.correo, "
        << "r.aerolinea, r.origen, r.destino, r.fecha_salida, r.horario, r.clase, r.asiento, r.estado, "
        << "r.equipaje_mano_piezas, r.equipaje_grande_piezas, r.cobro_total_equipaje "
        << "FROM tickets_equipaje t "
        << "INNER JOIN reservas r ON t.reserva_id = r.id ";

    if (soloDigitos) {
        sql << "WHERE t.id = " << atoi(criterio.c_str())
            << " OR t.codigo_ticket LIKE '%" << sqlEscape(criterio) << "%';";
    } else {
        sql << "WHERE UPPER(t.codigo_ticket) LIKE UPPER('%" << sqlEscape(criterio) << "%');";
    }

    if (db.query(db.conn, sql.str().c_str()) != 0) return;
    MYSQL_RES* res = db.store_result(db.conn);
    if (!res) return;

    int encontrados = 0;
    MYSQL_ROW row;
    while ((row = db.fetch_row(res))) {
        encontrados++;
        cout << "\n************************************************************************\n";
        cout << "  TICKET DE MALETA:      " << (row[1] ? row[1] : "") << " (ID #" << (row[0] ? row[0] : "") << ")\n";
        cout << "  Tipo / Pieza:          " << (row[2] ? row[2] : "") << " | Pieza #" << (row[3] ? row[3] : "") << "\n";
        cout << "  Costo de esta maleta:  Q" << fixed << setprecision(2) << (row[5] ? atof(row[5]) : 0.0) << "\n";
        cout << "------------------------------------------------------------------------\n";
        cout << "  DUENO DE LA MALETA:    " << (row[8] ? row[8] : "") << " (Doc: " << (row[9] ? row[9] : "") << ")\n";
        cout << "  Contacto:              Tel: " << (row[12] ? row[12] : "") << " | Correo: " << (row[13] ? row[13] : "") << "\n";
        cout << "  Vuelo / Ruta:          " << (row[14] ? row[14] : "") << " | " << (row[15] ? row[15] : "") << " -> " << (row[16] ? row[16] : "") << "\n";
        cout << "  Estado Ventanilla:     " << (row[21] ? row[21] : "") << "\n";
        cout << "************************************************************************\n";
    }
    db.free_result(res);
    if (encontrados == 0) cout << "No se encontro ningun ticket con '" << criterio << "'.\n";
}

void buscarClienteYEquipaje() {
    cout << "\n=== [SUPERVISOR] BUSCAR PASAJERO Y SUS MALETAS ===\n";
    string criterio = leerTexto("Ingrese Nombre, Documento o ID del pasajero: ");
    bool soloDigitos = !criterio.empty() && all_of(criterio.begin(), criterio.end(), ::isdigit);

    ostringstream sql;
    sql << "SELECT id, nombre, fecha_nacimiento, edad, nacionalidad, documento, vencimiento_pasaporte, "
        << "origen, destino, fecha_salida, fecha_regreso, horario, clase, preferencia_directo, aerolinea, "
        << "presupuesto, cantidad_pasajeros, asiento, equipaje_mano_piezas, equipaje_grande_piezas, "
        << "cobro_extra_mano, cobro_extra_grande, cobro_total_equipaje, telefono, correo, pais_residencia, "
        << "emergencia, metodo_pago, requiere_factura, viajero_frecuente, estado "
        << "FROM reservas WHERE ";
    if (soloDigitos) {
        sql << "id = " << atoi(criterio.c_str()) << " OR documento LIKE '%" << sqlEscape(criterio) << "%';";
    } else {
        sql << "UPPER(nombre) LIKE UPPER('%" << sqlEscape(criterio) << "%') OR UPPER(documento) LIKE UPPER('%" << sqlEscape(criterio) << "%');";
    }

    if (db.query(db.conn, sql.str().c_str()) != 0) return;
    MYSQL_RES* res = db.store_result(db.conn);
    if (!res) return;

    int count = 0;
    MYSQL_ROW row;
    while ((row = db.fetch_row(res))) {
        count++;
        mostrarReserva(filaAReserva(row), true);
    }
    db.free_result(res);
    if (count == 0) cout << "No se encontraron pasajeros para '" << criterio << "'.\n";
}

void abrirInterfazGraficaHTML() {
    cout << "\nAbriendo la interfaz grafica interactiva (index.html) en su navegador...\n";
#ifdef _WIN32
    system("start \"\" \"index.html\"");
#endif
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    if (!conectarEInicializarMySQL()) {
        cout << "\nPresione Enter para salir...";
        cin.get();
        return 1;
    }

    int opcion;
    do {
        cout << "\n========== SISTEMA DE AEROLINEA, VUELOS Y EQUIPAJE (MySQL + HTML) ==========\n";
        cout << "1. [INTERFAZ GRAFICA HTML] Abrir panel visual en el navegador (index.html)\n";
        cout << "2. [ROL PASAJERO] Registrar nuevo pasajero y sus maletas\n";
        cout << "3. [ROL VENTANILLA] Corregir datos mal ingresados y confirmar con Check [✓]\n";
        cout << "4. [ROL SUPERVISOR] Buscar pasajeros por Vuelo y ver codigos de sus maletas\n";
        cout << "5. [ROL SUPERVISOR] Buscar por Codigo de Ticket de Maleta (ver propietario)\n";
        cout << "6. [ROL SUPERVISOR] Buscar por Nombre o Documento de Pasajero\n";
        cout << "0. Salir\n";
        opcion = leerEntero("Seleccione una opcion: ", 0, 6);

        switch (opcion) {
            case 1: abrirInterfazGraficaHTML(); break;
            case 2: rolPasajeroRegistrar(); break;
            case 3: rolVentanillaCorregirYConfirmar(); break;
            case 4: rolSupervisorBuscarPorVuelo(); break;
            case 5: buscarPorTicketEquipaje(); break;
            case 6: buscarClienteYEquipaje(); break;
            case 0: cout << "Saliendo del sistema. Todos los datos estan guardados en MySQL.\n"; break;
        }
    } while (opcion != 0);
    return 0;
}
