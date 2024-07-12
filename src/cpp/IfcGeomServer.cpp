//#define STANDALONE_TEST

#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <map>
// Disable Logging so nothing unwanted gets written to stdout
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_OFF
#include <spdlog/spdlog.h>

#include "parsing/IfcLoader.h"
#include "schema/IfcSchemaManager.h"
#include "geometry/IfcGeometryProcessor.h"
#include "schema/ifc-schema.h"

// NB: Streams are only re-opened as binary when compiled with MSVC currently.
//     It is unclear what the correct behaviour would be compiled with e.g MinGW
#if defined(_MSC_VER)
#define SET_BINARY_STREAMS
#endif

#ifdef SET_BINARY_STREAMS
#include <io.h>
#include <fcntl.h>
#endif

template <typename T>
union data_field {
    char buffer[sizeof(T)];
    T value;
};

template <typename T>
T sread(std::istream& s) {
    data_field<T> data;
    s.read(data.buffer, sizeof(T));
    return data.value;
}

template <>
std::string sread(std::istream& s) {
	int32_t len = sread<int32_t>(s);
	char* buf = new char[len + 1];
	s.read(buf, len);
	buf[len] = 0;
	while (len++ % 4) s.get();
	std::string str(buf);
	delete[] buf;
	return str;
}

template <typename T>
std::string format_json(const T& t) {
	return "{}";
	//return boost::lexical_cast<std::string>(t);
}

template <>
std::string format_json(const std::string& s) {
	// NB: No escaping whatsoever. Only use alphanumeric values.
	return "\"" + s + "\"";
}

template <>
std::string format_json(const double& d) {
	std::stringstream ss;
	ss << std::setprecision(std::numeric_limits<double>::digits10) << d;
	return ss.str();
}

static std::streambuf* stdout_orig, * stdout_redir;

template <typename T>
void swrite(std::ostream& s, T t) {
	char buf[sizeof(T)];
	memcpy(buf, &t, sizeof(T));
	s.write(buf, sizeof(T));
}

template <>
void swrite(std::ostream& s, std::string t) {
	int32_t len = (int32_t)t.size();
	swrite(s, len);
	s.write(t.c_str(), len);
	while (len++ % 4) s.put(0);
}

template <typename T, typename U>
void swrite_array(std::ostream& s, const std::vector<U>& us) {
#ifdef STANDALONE_TEST
	std::cout << std::endl;
	for (auto value : us) {
		std::cout << value << ", ";
	}
	std::cout << std::endl;
#else
	if (std::is_same<T, U>::value) {
		swrite(s, std::string((char*)us.data(), us.size() * sizeof(U)));
	}
	else {
		std::vector<T> ts;
		ts.reserve(us.size());
		for (auto& u : us) {
			ts.push_back((T)u);
		}
		swrite_array<T, T>(s, ts);
	}
#endif
}

class Command {
protected:
	virtual void read_content(std::istream& s) = 0;
	virtual void write_content(std::ostream& s) = 0;
	int32_t iden;
	int32_t len;
public:
	void read(std::istream& s) {
		len = sread<int32_t>(s);
		read_content(s);
	}

	void write(std::ostream& s) {
		std::cout.rdbuf(stdout_orig);
		swrite(s, iden);
		std::ostringstream oss;
		write_content(oss);
		swrite(s, oss.str());
		s.flush();
		std::cout.rdbuf(stdout_redir);
	}

	Command(int32_t iden) : iden(iden) {}
};

const int32_t HELLO = 0xff00;
const int32_t IFC_MODEL = HELLO + 1;
const int32_t GET = IFC_MODEL + 1;
const int32_t ENTITY = GET + 1;
const int32_t MORE = ENTITY + 1;
const int32_t NEXT = MORE + 1;
const int32_t BYE = NEXT + 1;
const int32_t GET_LOG = BYE + 1;
const int32_t LOG = GET_LOG + 1;
const int32_t DEFLECTION = LOG + 1;
const int32_t SETTING = DEFLECTION + 1;

class Hello : public Command {
private:
	std::string str;
protected:
	void read_content(std::istream& s) {
		str = sread<std::string>(s);
	}
	void write_content(std::ostream& s) {
		swrite(s, str);
	}
public:
	const std::string& string() { return str; }
	Hello() : Command(HELLO), str("IFCJS-0.0.54-0") {}
};

class More : public Command {
private:
	bool more;
protected:
	void read_content(std::istream& s) {
		more = sread<int32_t>(s) == 1;
	}
	void write_content(std::ostream& s) {
		swrite<int32_t>(s, more ? 1 : 0);
	}
public:
	More(bool more) : Command(MORE), more(more) {}
};

class IfcModel : public Command {
private:
	std::string str;
protected:
	void read_content(std::istream& s) {
		str = sread<std::string>(s);
	}
	void write_content(std::ostream& s) {
		swrite(s, str);
	}
public:
	const std::string& string() { return str; }
	IfcModel() : Command(IFC_MODEL) {};
};

class Get : public Command {
protected:
	void read_content(std::istream& /*s*/) {}
	void write_content(std::ostream& /*s*/) {}
public:
	Get() : Command(GET) {};
};

class GetLog : public Command {
protected:
	void read_content(std::istream& /*s*/) {}
	void write_content(std::ostream& /*s*/) {}
public:
	GetLog() : Command(GET_LOG) {};
};

class WriteLog : public Command {
private:
	std::string str;
protected:
	void read_content(std::istream& s) {
		str = sread<std::string>(s);
	}
	void write_content(std::ostream& s) {
		swrite(s, str);
	}
public:
	WriteLog(const std::string& str) : Command(LOG), str(str) {};
};

class EntityExtension {
protected:
	bool trailing_, opened_;
	std::stringstream json_;

	template <typename T>
	void put_json(const std::string& k, T v) {
		if (!opened_) {
			json_ << "{";
			opened_ = true;
		}
		if (trailing_) {
			json_ << ",";
		}
		json_ << format_json(k) << ":" << format_json(v);
		trailing_ = true;
	}
public:
	EntityExtension()
		: trailing_(false)
		, opened_(false)
	{}

	void write_contents(std::ostream& s) {
		if (opened_) {
			json_ << "}";
		}

		// We do a 4-byte manual alignment
		std::string payload = json_.str();
		s << payload;
		if (payload.size() % 4) {
			s << std::string(4 - (payload.size() % 4), ' ');
		}
	}
};

struct IfcElement {
	uint32_t expressId;
	uint32_t parentId;

	std::string guid;
	std::string name;
	std::string type;

	webifc::geometry::IfcFlatMesh flatMesh;
};

class IfcGeomIterator {
private:
	webifc::schema::IfcSchemaManager* schemaManager;
	webifc::parsing::IfcLoader* loader;
	webifc::geometry::IfcGeometryProcessor* geometryProcessor;

	std::vector<uint32_t> expressIds;
	uint32_t position;

public:
	IfcGeomIterator(webifc::schema::IfcSchemaManager* schemaManager, webifc::parsing::IfcLoader* loader, webifc::geometry::IfcGeometryProcessor* geometryProcessor)
		: schemaManager(schemaManager), loader(loader), geometryProcessor(geometryProcessor) {
		auto const& geometryLoader = geometryProcessor->GetLoader();

		for (auto expressId : loader->GetAllLines()) {
			auto lineType = loader->GetLineType(expressId);
			if (!schemaManager->IsIfcElement(lineType)) {
				continue;
			}

			loader->MoveToArgumentOffset(expressId, 6);
			if (loader->GetTokenType() != webifc::parsing::IfcTokenType::REF) {
				continue;
			}

			expressIds.push_back(expressId);
		}

		position = 0;
	}

	bool HasMore() {
		return position < expressIds.size();
	}

	IfcElement GetNext() {
		IfcElement ifcElement;

		ifcElement.expressId = expressIds.at(position++);

		loader->MoveToArgumentOffset(ifcElement.expressId, 0);
		ifcElement.guid = loader->GetDecodedStringArgument();

		loader->MoveToArgumentOffset(ifcElement.expressId, 2);
		ifcElement.name = loader->GetStringArgument();

		auto lineType = loader->GetLineType(ifcElement.expressId);
		ifcElement.type = schemaManager->IfcTypeCodeToType(lineType);

		try {
			auto const& flatMesh = geometryProcessor->GetFlatMesh(ifcElement.expressId);
			if (flatMesh.geometries.size() == 0) {
				return GetNext();
			}

			ifcElement.flatMesh = flatMesh;
		}
		catch (...) {
			std::cout << "ERROR";
			return GetNext();
		}

		return ifcElement;
	}
};

class Entity : public Command {
private:
	IfcElement* ifcElement;
	webifc::schema::IfcSchemaManager* schemaManager;
	webifc::parsing::IfcLoader* loader;
	webifc::geometry::IfcGeometryProcessor* geometryProcessor;

	bool append_line_data;
	EntityExtension* eext_;

	void writeTransformation(glm::dmat4 m, std::ostream& s) {
		const double matrix_array[16] = {
			m[0][0], m[1][0], m[2][0], m[3][0],
			m[0][1], m[1][1], m[2][1], m[3][1],
			m[0][2], m[1][2], m[2][2], m[3][2],
			m[0][3], m[1][3], m[2][3], m[3][3],
		};

		swrite(s, std::string((char*)matrix_array, 16 * sizeof(double)));
	}

protected:
	void read_content(std::istream& /*s*/) {}
	void write_content(std::ostream& s) {
		swrite<int32_t>(s, ifcElement->expressId);
		swrite(s, ifcElement->guid);
		swrite(s, ifcElement->name);
		swrite(s, ifcElement->type);
		swrite<int32_t>(s, ifcElement->parentId); // TODO

		loader->MoveToArgumentOffset(ifcElement->expressId, 5);
		auto placementRef = loader->GetRefArgument();
		auto globalTransformation = geometryProcessor->GetLoader().GetLocalPlacement(placementRef);

		writeTransformation(globalTransformation, s);

		auto inverseTransformation = glm::inverse(globalTransformation);

		// TODO
		const std::string& representation_id = "Test Representation ID";
		const int integer_representation_id = atoi(representation_id.c_str());
		swrite<int32_t>(s, (int32_t)integer_representation_id);

		std::vector<double> vertices;
		std::vector<float> normals;
		std::vector<int32_t> indices;

		std::vector<float> colors;
		std::vector<int32_t> color_indices;
		uint32_t colorIndex = 0;

		for (auto &geometry : ifcElement->flatMesh.geometries) {
			auto currentTransformation = geometry.transformation;
			auto combinedTransformation = inverseTransformation * currentTransformation;
			auto combinedNormalTransformation = glm::transpose(glm::inverse(combinedTransformation)); // according to: https://www.scratchapixel.com/lessons/mathematics-physics-for-computer-graphics/geometry/transforming-normals.html

			auto &flatGeom = geometryProcessor->GetGeometry(geometry.geometryExpressID);
			auto &vertexData = flatGeom.vertexData;
			auto indexOffset = vertices.size() / 3;

			for (int i = 0; i < vertexData.size(); i += 6) {
				glm::vec4 position(vertexData.at(i), vertexData.at(i + 1), vertexData.at(i + 2), 1.0f);
				glm::vec4 normal(vertexData.at(i + 3), vertexData.at(i + 4), vertexData.at(i + 5), 1.0f);
				auto untransformedPosition = combinedTransformation * position;
				auto untransformedNormal = combinedNormalTransformation * normal;

				vertices.push_back(untransformedPosition.x);
				vertices.push_back(untransformedPosition.y);
				vertices.push_back(untransformedPosition.z);

				normals.push_back(untransformedNormal.x);
				normals.push_back(untransformedNormal.y);
				normals.push_back(untransformedNormal.z);
			}

			auto color = geometry.color;

			for (auto index : flatGeom.indexData) {
				indices.push_back(index + indexOffset);
			}

			if (color.r != -1 && color.g != -1 && color.b != -1 && color.a != -1) {
				colors.push_back(color.r);
				colors.push_back(color.g);
				colors.push_back(color.b);
				colors.push_back(color.a);

				for (int i = 0; i < flatGeom.indexData.size() / 3; i++) {
					color_indices.push_back(colorIndex);
				}

				colorIndex += 1;
			}
			else {
				for (int i = 0; i < flatGeom.indexData.size() / 3; i++) {
					color_indices.push_back(-1);
				}
			}
		}

		swrite_array<double>(s, vertices);
		swrite_array<float>(s, normals);
		swrite_array<int32_t>(s, indices);

		// TODO append_line_data

		swrite(s, std::string((char*)colors.data(), colors.size() * sizeof(float)));
		swrite(s, std::string((char*)color_indices.data(), color_indices.size() * sizeof(int32_t)));

		if (eext_) {
			eext_->write_contents(s);
		}

		geometryProcessor->Clear();
	}

	glm::dmat4* extractRotation(glm::dmat4* tranformation) {
		auto _transformation = *tranformation;
		auto sx = glm::length(_transformation[0]);
		auto sy = glm::length(_transformation[1]);
		auto sz = glm::length(_transformation[2]);

		auto currentRotation = new glm::dmat4(_transformation[0] / sx, _transformation[1] / sy, _transformation[2] / sz, glm::dvec4(0));

		return currentRotation;
	}

public:
	Entity(IfcElement* ifcElement, webifc::schema::IfcSchemaManager* schemaManager, webifc::parsing::IfcLoader* loader, webifc::geometry::IfcGeometryProcessor* geometryProcessor, EntityExtension* eext = 0)
		: Command(ENTITY), append_line_data(false), eext_(eext), ifcElement(ifcElement), geometryProcessor(geometryProcessor), schemaManager(schemaManager), loader(loader) {};
};

class Next : public Command {
protected:
	void read_content(std::istream& /*s*/) {}
	void write_content(std::ostream& /*s*/) {}
public:
	Next() : Command(NEXT) {};
};

class Bye : public Command {
protected:
	void read_content(std::istream& /*s*/) {}
	void write_content(std::ostream& /*s*/) {}
public:
	Bye() : Command(BYE) {};
};

class Deflection : public Command {
private:
	double deflection_;
protected:
	void read_content(std::istream& s) {
		deflection_ = sread<double>(s);
	}
	void write_content(std::ostream& s) {
		swrite(s, deflection_);
	}
public:
	Deflection(double d = 0.) : Command(DEFLECTION), deflection_(d) {};
	double deflection() const { return deflection_; }
};

class Setting : public Command {
private:
	uint32_t id_;
	uint32_t value_;
protected:
	void read_content(std::istream& s) {
		id_ = sread<uint32_t>(s);
		value_ = sread<uint32_t>(s);
	}
	void write_content(std::ostream& s) {
		swrite(s, id_);
		swrite(s, value_);
	}
public:
	Setting(uint32_t k = 0, uint32_t v = 0) : Command(SETTING), id_(k), value_(v) {};
	uint32_t id() const { return id_; }
	uint32_t value() const { return value_; }
};

static const std::string TOTAL_SURFACE_AREA = "TOTAL_SURFACE_AREA";
static const std::string TOTAL_SHAPE_VOLUME = "TOTAL_SHAPE_VOLUME";
static const std::string SURFACE_AREA_ALONG_X = "SURFACE_AREA_ALONG_X";
static const std::string SURFACE_AREA_ALONG_Y = "SURFACE_AREA_ALONG_Y";
static const std::string SURFACE_AREA_ALONG_Z = "SURFACE_AREA_ALONG_Z";
static const std::string WALKABLE_SURFACE_AREA = "WALKABLE_SURFACE_AREA";
static const std::string LARGEST_FACE_AREA = "LARGEST_FACE_AREA";
static const std::string LARGEST_FACE_DIRECTION = "LARGEST_FACE_DIRECTION";
static const std::string BOUNDING_BOX_SIZE_ALONG_ = "BOUNDING_BOX_SIZE_ALONG_";
static const std::array<std::string, 3> XYZ = { "X", "Y", "Z" };

class QuantityWriter_v0 : public EntityExtension {
private:
public:
	QuantityWriter_v0()
	{
		put_json(TOTAL_SURFACE_AREA, 0.);
		put_json(TOTAL_SHAPE_VOLUME, 0.);
		put_json(WALKABLE_SURFACE_AREA, 0.);
	}
};

class QuantityWriter_v1 : public EntityExtension {
private:
	IfcElement* ifcElement;
public:
	QuantityWriter_v1(IfcElement* ifcElement) {
		double a, b, c, largest_face_area = 0.;

		put_json(TOTAL_SURFACE_AREA, 0);
		put_json(TOTAL_SHAPE_VOLUME, 0);
		put_json(SURFACE_AREA_ALONG_X, 0);
		put_json(SURFACE_AREA_ALONG_Y, 0);
		put_json(SURFACE_AREA_ALONG_Z, 0);
		//put_json(BOUNDING_BOX_SIZE_ALONG_ + XYZ[i], bsz);

		/*if (largest_face_dir) {
			put_json(LARGEST_FACE_DIRECTION, *largest_face_dir);
			put_json(LARGEST_FACE_AREA, largest_face_area);
		}*/
	}
};

struct LoaderSettings
{
	bool OPTIMIZE_PROFILES = false;
	bool COORDINATE_TO_ORIGIN = false;
	uint16_t CIRCLE_SEGMENTS = 12;
	uint32_t TAPE_SIZE = 67108864; // probably no need for anyone other than web-ifc devs to change this
	uint32_t MEMORY_LIMIT = 2147483648;
	uint16_t LINEWRITER_BUFFER = 10000;
};

int main() {
	// Disable Logging so nothing unwanted gets written to stdout
	spdlog::set_level(spdlog::level::off);

	// Redirect stdout to this stream, so that involuntary 
	// writes to stdout do not interfere with our protocol.
	std::ostringstream oss;
	stdout_redir = oss.rdbuf();
	stdout_orig = std::cout.rdbuf();
	std::cout.rdbuf(stdout_redir);

	bool emit_quantities = false;

#ifdef SET_BINARY_STREAMS
	_setmode(_fileno(stdout), _O_BINARY);
	std::cout.setf(std::ios_base::binary);
	_setmode(_fileno(stdin), _O_BINARY);
	std::cin.setf(std::ios_base::binary);
#endif

	double deflection = 1.e-3;
	bool has_more = false;

	IfcGeomIterator* iterator = 0;

	webifc::schema::IfcSchemaManager schemaManager;
	webifc::parsing::IfcLoader* loader = 0;
	webifc::geometry::IfcGeometryProcessor* geometryProcessor = 0;

	Hello().write(std::cout);

#ifdef STANDALONE_TEST
	std::vector<int32_t> messages;
	messages.push_back(IFC_MODEL);
	messages.push_back(GET);
	messages.push_back(NEXT);
	messages.push_back(MORE);
	messages.push_back(GET);
	messages.push_back(NEXT);
#endif

	int exit_code = 0;

#ifdef STANDALONE_TEST
	for(int32_t msg_type : messages) {
#else
	for (;;) {
		const int32_t msg_type = sread<int32_t>(std::cin);
#endif
		switch (msg_type) {
		case IFC_MODEL: {
			IfcModel m;

#ifdef STANDALONE_TEST
			std::ifstream fileStream("C:/Users/andreas/Downloads/T�r_3.ifc");
			if (fileStream.is_open()) {
				m.read(fileStream);
			}
#else
			m.read(std::cin);
#endif

			LoaderSettings set;

			loader = new webifc::parsing::IfcLoader(set.TAPE_SIZE, set.MEMORY_LIMIT, set.LINEWRITER_BUFFER, schemaManager);

			loader->LoadFile([&](char* dest, size_t sourceOffset, size_t destSize)
				{
					uint32_t length = std::min(m.string().size() - sourceOffset, destSize);
					memcpy(dest, &m.string()[sourceOffset], length);

					return length; });

			std::array<double, 16> reverseNormalizeIfc = {
				1, 0, 0, 0,
				0, 0, 1, 0,
				0, -1, 0, 0,
				0, 0, 0, 1,
			};

			auto defaultColor = new glm::dvec4(-1.0);

			geometryProcessor = new webifc::geometry::IfcGeometryProcessor(*loader, schemaManager, set.CIRCLE_SEGMENTS, set.COORDINATE_TO_ORIGIN, false);
			geometryProcessor->SetTransformation(reverseNormalizeIfc);
			geometryProcessor->SetDefaultColor(defaultColor);


			iterator = new IfcGeomIterator(&schemaManager, loader, geometryProcessor);
			has_more = iterator->HasMore();

			More(has_more).write(std::cout);
			continue;
		}
		case GET: {
#ifndef STANDALONE_TEST
			Get g; g.read(std::cin);
#endif

			if (!has_more) {
				exit_code = 1;
				break;
			}
			/*const IfcGeom::TriangulationElement* geom = static_cast<const IfcGeom::TriangulationElement*>(iterator->get());
			std::unique_ptr<EntityExtension> eext;
			if (emit_quantities) {
				eext.reset(new QuantityWriter_v1(iterator->get_native()));
			}
			else {
				eext.reset(new QuantityWriter_v0(iterator->get_native()));
			}
			Entity(geom, eext.get()).write(std::cout);*/

			auto ifcElement = iterator->GetNext();

			std::unique_ptr<EntityExtension> eext;
			eext.reset(new QuantityWriter_v1(&ifcElement));

			Entity(&ifcElement, &schemaManager, loader, geometryProcessor, eext.get()).write(std::cout);

			continue;
		}
		case NEXT: {
#ifndef STANDALONE_TEST
			Next n; n.read(std::cin);
#endif

			has_more = iterator->HasMore();
			if (!has_more) {
				//delete file;
				delete iterator;
				//file = 0;
				iterator = 0;
			}
			More(has_more).write(std::cout);
			continue;
		}
		case GET_LOG: {
			GetLog gl; gl.read(std::cin);
			WriteLog("").write(std::cout); // TODO
			continue;
		}
		case BYE: {
			Bye().write(std::cout);
			exit_code = 0;
			break;
		}
		case DEFLECTION: {
			Deflection d; d.read(std::cin);
			if (!iterator) {
				deflection = d.deflection();
				continue;
			}
			else {
				exit_code = 1;
				break;
			}
		}
		case SETTING: {
			Setting s; s.read(std::cin);
			if (!iterator) {
				// TODO
				//setting_pairs.push_back(std::make_pair(s.id(), s.value()));
				continue;
			}
			else {
				exit_code = 1;
				break;
			}
		}
		default:
			exit_code = 1;
			break;
		}
		break;
	}
	std::cout.rdbuf(stdout_orig);
	return exit_code;
}