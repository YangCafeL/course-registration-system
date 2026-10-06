/**
 * ==========================================================================
 *  课程报名管理系统（C++ 控制台交互版 v2.0）
 * --------------------------------------------------------------------------
 *  开发环境：Visual Studio 2022（C++17），单文件 main.cpp，可直接编译运行。
 *
 *  一、功能特性（相对 v1.0 静态演示的升级）
 *     1. 账户系统：注册 / 登录 / 注销 / 修改密码；
 *        未登录不可进行报名、退课等操作，所有输入均有校验与明确错误提示
 *        （格式非法、用户名重复、密码不一致、学号已绑定、越权访问等）。
 *     2. 报名管理：添加报名人员（姓名、专业、绩点、联系电话）、
 *        按条件查询报名信息（学号/姓名/课程编号/课程名称/状态/课程类型）、
 *        修改报名记录（更换课程、置为退课、恢复有效）、修改人员信息、
 *        删除报名人员（级联清理报名记录与关联登录账户）；
 *        每次操作完成后立即刷新显示最新数据。
 *     3. 数据持久化：所有变更即时写入 system_data.txt（管道分隔、分节存储，
 *        临时文件 + 改名方式落盘）；重新运行程序、注销后重新登录，数据均在。
 *     4. 完整交互闭环：菜单式“按钮 + 表单”，每个菜单项均实际执行，
 *        统一输出 [成功] / [失败] / [提示] / [权限不足] 反馈。
 *
 *  二、面向对象设计
 *     封装：数据成员一律 private，通过公有 getter/setter 访问，接口内做
 *           合法性校验（绩点 0-5、容量为正、姓名非空、格式约束等）。
 *     继承：Person ──> Student / Teacher                     （不同角色的人员）
 *           Course ──> LectureCourse / LabCourse / OnlineCourse（不同类型课程）
 *     多态：Person::displayInfo / displayBrief
 *           Course::getTypeName / computeTuition / checkEnrollment /
 *                  requirementDesc / displayDetail —— 经基类指针运行期分派
 *
 *  三、类划分
 *     Person / Student / Teacher   人员层次
 *     Course / Lecture/Lab/Online  课程层次
 *     EnrollmentManager            报名核心业务、查询与统计
 *     UserAccount / AccountManager 账户与鉴权
 *     Storage                      持久化（读写 system_data.txt）
 *     SystemUI                     菜单界面、输入校验、权限门禁、操作反馈
 * ==========================================================================
 */

#include <iostream>     // 标准输入输出流
#include <string>       // std::string
#include <vector>       // std::vector
#include <algorithm>    // std::find / max / remove_if
#include <iomanip>      // setprecision / put_time
#include <sstream>      // ostringstream / istringstream
#include <ctime>        // time / tm（报名日期）
#include <map>          // map（分类统计）
#include <functional>   // std::function（输入校验器）
#include <fstream>      // ifstream / ofstream（数据持久化）
#include <cctype>       // isalnum / tolower
#include <cstdlib>      // std::exit

using namespace std;

/*-------------------- 类之间的前置声明（相互引用时使用） --------------------*/
class Student;
class Course;

/*============================= 枚举与通用工具 =============================*/

/**
 * @brief 选课/退课/改记录操作的结果枚举，用于精确区分各类边界情况
 */
enum class EnrollStatus {
    Success = 0,        // 操作成功
    StudentNotFound,    // 学生（报名人员）不存在
    CourseNotFound,     // 课程不存在
    AlreadyEnrolled,    // 重复选课 / 记录已处于有效状态
    CourseFull,         // 课程容量已满
    EligibilityNotMet,  // 不满足选课条件（如绩点不足）
    NotEnrolled,        // 未选修该课程，无法退课 / 记录已退课
    RecordNotFound,     // 未找到指定流水号的报名记录
    SameCourse           // 更换的课程与原课程相同
};

/**
 * @brief 操作结果 -> 中文描述（统一的失败/成功提示文案）
 */
string enrollStatusText(EnrollStatus status) {
    switch (status) {
    case EnrollStatus::Success:           return "成功";
    case EnrollStatus::StudentNotFound:   return "报名人员（学号）不存在";
    case EnrollStatus::CourseNotFound:    return "课程不存在";
    case EnrollStatus::AlreadyEnrolled:   return "重复选课，该课程已选修";
    case EnrollStatus::CourseFull:        return "课程容量已满";
    case EnrollStatus::EligibilityNotMet: return "不满足该课程的选课条件";
    case EnrollStatus::NotEnrolled:       return "尚未选修该课程（或记录已退课）";
    case EnrollStatus::RecordNotFound:    return "未找到该报名记录";
    case EnrollStatus::SameCourse:        return "目标课程与原课程相同";
    default:                              return "未知状态";
    }
}

/**
 * @brief 去除字符串首尾的空白字符（空格 / 制表符 / 回车）
 */
string trim(const string& s) {
    const string ws = " \t\r\n";
    size_t b = s.find_first_not_of(ws);
    if (b == string::npos) return "";
    size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

/**
 * @brief 是否全部为数字字符
 */
bool isDigits(const string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!isdigit((unsigned char)c)) return false;
    return true;
}

/**
 * @brief 简单口令哈希（FNV-1a + 固定盐，十六进制字符串）
 *        说明：教学演示用途；实际工程应使用 bcrypt / scrypt 等加盐慢哈希。
 */
string hashPassword(const string& password) {
    const string salted = "CRS2024::" + password;
    unsigned long long h = 14695981039346656037ULL;
    for (unsigned char ch : salted) {
        h ^= ch;
        h *= 1099511628211ULL;
    }
    ostringstream oss;
    oss << hex << h;
    return oss.str();
}

/**
 * @brief 当前系统日期（yyyy-mm-dd），用于记录报名时间
 */
string currentDateString() {
    time_t t = time(nullptr);
    tm tmBuf{};
#ifdef _MSC_VER
    localtime_s(&tmBuf, &t);                 // MSVC 安全版本
#else
    tm* p = localtime(&t);
    if (p) tmBuf = *p;
#endif
    ostringstream oss;
    oss << put_time(&tmBuf, "%Y-%m-%d");
    return oss.str();
}

/**
 * @brief 按分隔符拆分字符串（用于读取持久化文件）
 */
vector<string> split(const string& s, char delim) {
    vector<string> out;
    string cur;
    for (char c : s) {
        if (c == delim) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

/*============================== 输入校验器 ================================*/
/* 校验器签名：合法返回 true；否则通过 err 返回中文错误提示。                */

bool vRequired(const string& s, string& err) {
    if (s.empty()) { err = "内容不能为空"; return false; }
    if (s.find('|') != string::npos) { err = "不能包含竖线字符 |"; return false; }
    return true;
}

bool vUsername(const string& s, string& err) {
    if (s.size() < 3 || s.size() > 16) { err = "用户名长度需为 3-16 位"; return false; }
    for (char c : s)
        if (!(isalnum((unsigned char)c) || c == '_')) {
            err = "用户名只能包含字母、数字或下划线"; return false;
        }
    return true;
}

bool vPassword(const string& s, string& err) {
    if (s.size() < 6) { err = "密码长度至少 6 位"; return false; }
    if (s.size() > 32) { err = "密码长度不能超过 32 位"; return false; }
    if (s.find('|') != string::npos) { err = "密码不能包含竖线字符 |"; return false; }
    return true;
}

bool vPersonId(const string& s, string& err) {
    if (s.size() < 4 || s.size() > 16) { err = "学号/工号长度需为 4-16 位"; return false; }
    for (char c : s)
        if (!isalnum((unsigned char)c)) { err = "学号/工号只能包含字母或数字"; return false; }
    return true;
}

bool vName(const string& s, string& err) {
    if (s.empty()) { err = "姓名不能为空"; return false; }
    if (s.size() > 40) { err = "姓名过长（不超过 40 字节）"; return false; }
    if (s.find('|') != string::npos) { err = "不能包含竖线字符 |"; return false; }
    return true;
}

bool vMajor(const string& s, string& err) {
    if (s.empty()) { err = "专业不能为空"; return false; }
    if (s.size() > 60) { err = "专业描述过长（不超过 60 字节）"; return false; }
    if (s.find('|') != string::npos) { err = "不能包含竖线字符 |"; return false; }
    return true;
}

bool vGpa(const string& s, string& err) {
    istringstream iss(s);
    double d = 0.0;
    if (!(iss >> d)) { err = "绩点必须是数字，例如 3.50"; return false; }
    char extra = 0;
    if (iss >> extra) { err = "绩点格式非法，只能输入一个数字"; return false; }
    if (d < 0.0 || d > 5.0) { err = "绩点必须在 0.00 - 5.00 之间"; return false; }
    return true;
}

bool vPhone(const string& s, string& err) {
    if (s.size() < 7 || s.size() > 15) { err = "联系电话长度需为 7-15 位"; return false; }
    int digits = 0;
    for (char c : s) {
        if (isdigit((unsigned char)c)) ++digits;
        else if (c != '-') { err = "联系电话只能包含数字或短横线 -"; return false; }
    }
    if (digits < 7) { err = "联系电话至少包含 7 位数字"; return false; }
    return true;
}

bool vPositiveInt(const string& s, string& err) {
    if (!isDigits(s)) { err = "请输入正整数数字"; return false; }
    return true;
}

/*=============================== 人员类层次 ================================*/

/**
 * @brief 人员抽象基类（数据成员 private，仅暴露公有接口 —— 封装）
 */
class Person {
private:
    string id_;        // 编号：学号或工号
    string name_;      // 姓名
    string phone_;     // 联系电话
public:
    Person(const string& id, const string& name, const string& phone = "")
        : id_(id), name_(name), phone_(phone) {}

    virtual ~Person() = default;   // 虚析构：保证经基类指针正确析构

    /* ---------- 封装：公有访问接口 ---------- */
    const string& getId() const { return id_; }
    const string& getName() const { return name_; }
    const string& getPhone() const { return phone_; }
    void setName(const string& name) { if (!name.empty()) name_ = name; }
    void setPhone(const string& phone) { phone_ = phone; }

    /* ---------- 多态接口 ---------- */
    virtual string getRoleName() const = 0;   // 角色名称（纯虚）
    virtual void displayInfo() const = 0;     // 完整信息展示（纯虚）

    void displayBrief() const {               // 内部调用虚函数，运行期多态
        cout << "【" << getRoleName() << "】" << id_ << " " << name_;
    }
};

/**
 * @brief 报名人员（学生）：自身维护已选课程与学分，由管理系统回调更新
 */
class Student : public Person {
private:
    string major_;                // 专业
    double gpa_;                  // 绩点（0.0 ~ 5.0）
    vector<string> selectedIds_;  // 已选课程编号
    int selectedCredits_;         // 已选总学分
public:
    Student(const string& id, const string& name, const string& major,
            double gpa, const string& phone = "")
        : Person(id, name, phone), major_(major), gpa_(gpa), selectedCredits_(0) {
        if (gpa_ < 0.0) gpa_ = 0.0;   // 构造时防御性校验（封装）
        if (gpa_ > 5.0) gpa_ = 5.0;
    }

    /* ---------- 封装：访问接口（setter 内含校验） ---------- */
    const string& getMajor() const { return major_; }
    double getGpa() const { return gpa_; }
    int getSelectedCredits() const { return selectedCredits_; }
    const vector<string>& getSelectedIds() const { return selectedIds_; }
    void setMajor(const string& major) { if (!major.empty()) major_ = major; }
    void setGpa(double gpa) { if (gpa >= 0.0 && gpa <= 5.0) gpa_ = gpa; }

    bool hasSelected(const string& courseId) const {
        return find(selectedIds_.begin(), selectedIds_.end(), courseId) != selectedIds_.end();
    }

    bool addCourse(const string& courseId, int credits) {   // 选课成功回调
        if (hasSelected(courseId)) return false;
        selectedIds_.push_back(courseId);
        selectedCredits_ += credits;
        return true;
    }

    bool removeCourse(const string& courseId, int credits) { // 退课成功回调
        auto it = find(selectedIds_.begin(), selectedIds_.end(), courseId);
        if (it == selectedIds_.end()) return false;
        selectedIds_.erase(it);
        selectedCredits_ -= credits;
        if (selectedCredits_ < 0) selectedCredits_ = 0;
        return true;
    }

    /* ---------- 多态实现 ---------- */
    string getRoleName() const override { return "学生"; }

    void displayInfo() const override {
        cout << "--------------------------------------------------\n";
        cout << "角色类型 : 学生（报名人员）\n";
        cout << "学    号 : " << getId() << "\n";
        cout << "姓    名 : " << getName() << "\n";
        cout << "专    业 : " << major_ << "\n";
        cout << "绩    点 : " << fixed << setprecision(2) << gpa_ << "\n";
        cout << "联系电话 : " << (getPhone().empty() ? string("未填写") : getPhone()) << "\n";
        cout << "已选门数 : " << selectedIds_.size() << " 门\n";
        cout << "已选学分 : " << selectedCredits_ << " 学分\n";
        cout << "课程编号 : ";
        if (selectedIds_.empty()) cout << "（暂无）";
        for (size_t i = 0; i < selectedIds_.size(); ++i) {
            cout << selectedIds_[i];
            if (i + 1 < selectedIds_.size()) cout << ", ";
        }
        cout << "\n--------------------------------------------------\n";
    }
};

/**
 * @brief 教师（与 Student 共同构成“不同角色的人员”继承层次）
 */
class Teacher : public Person {
private:
    string title_;               // 职称
    string department_;          // 院系
    vector<string> courseIds_;   // 承担课程编号
public:
    Teacher(const string& id, const string& name, const string& title,
            const string& department, const string& phone = "")
        : Person(id, name, phone), title_(title), department_(department) {}

    const string& getTitle() const { return title_; }
    const string& getDepartment() const { return department_; }
    const vector<string>& getCourseIds() const { return courseIds_; }

    bool hasCourse(const string& courseId) const {
        return find(courseIds_.begin(), courseIds_.end(), courseId) != courseIds_.end();
    }

    bool addCourseId(const string& courseId) {
        if (courseId.empty() || hasCourse(courseId)) return false;
        courseIds_.push_back(courseId);
        return true;
    }

    string getRoleName() const override { return "教师"; }

    void displayInfo() const override {
        cout << "--------------------------------------------------\n";
        cout << "角色类型 : 教师\n";
        cout << "工    号 : " << getId() << "\n";
        cout << "姓    名 : " << getName() << "\n";
        cout << "职    称 : " << title_ << "\n";
        cout << "所在院系 : " << department_ << "\n";
        cout << "承担课程 : ";
        if (courseIds_.empty()) cout << "（暂无）";
        for (size_t i = 0; i < courseIds_.size(); ++i) {
            cout << courseIds_[i];
            if (i + 1 < courseIds_.size()) cout << ", ";
        }
        cout << "\n--------------------------------------------------\n";
    }
};

/*=============================== 课程类层次 ================================*/

/**
 * @brief 课程抽象基类（继承 + 多态的核心）
 */
class Course {
private:
    string courseId_;      // 课程编号
    string courseName_;    // 课程名称
    string instructor_;    // 授课教师
    int credits_;          // 学分
    int capacity_;         // 容量上限
    string schedule_;      // 上课时间
    double creditPrice_;   // 每学分学费
public:
    Course(const string& id, const string& name, const string& instructor,
           int credits, int capacity, const string& schedule, double creditPrice)
        : courseId_(id), courseName_(name), instructor_(instructor),
          credits_(credits), capacity_(capacity), schedule_(schedule),
          creditPrice_(creditPrice) {}

    virtual ~Course() = default;

    /* ---------- 封装：访问接口 ---------- */
    const string& getId() const { return courseId_; }
    const string& getName() const { return courseName_; }
    const string& getInstructor() const { return instructor_; }
    int getCredits() const { return credits_; }
    int getCapacity() const { return capacity_; }
    const string& getSchedule() const { return schedule_; }
    double getCreditPrice() const { return creditPrice_; }
    void setSchedule(const string& s) { if (!s.empty()) schedule_ = s; }
    void setCapacity(int c) { if (c > 0) capacity_ = c; }

    /* ---------- 多态接口 ---------- */
    virtual string getTypeName() const = 0;                         // 纯虚
    virtual double computeTuition() const = 0;                      // 纯虚
    virtual bool checkEnrollment(const Student&) const { return true; }  // 选课资格
    virtual string requirementDesc() const { return "无附加要求"; }       // 条件描述
    virtual void displayDetail() const;                             // 详情（公共段）
};

void Course::displayDetail() const {
    cout << "--------------------------------------------------\n";
    cout << "课程编号 : " << courseId_ << "\n";
    cout << "课程名称 : " << courseName_ << "\n";
    cout << "课程类型 : " << getTypeName() << "\n";                          // 多态
    cout << "授课教师 : " << instructor_ << "\n";
    cout << "学    分 : " << credits_ << " 学分\n";
    cout << "上课时间 : " << schedule_ << "\n";
    cout << "容量上限 : " << capacity_ << " 人\n";
    cout << "选课条件 : " << requirementDesc() << "\n";                      // 多态
    cout << "应缴学费 : " << fixed << setprecision(1) << computeTuition() << " 元\n";  // 多态
}

/** @brief 理论课：学费 = 学分 x 单价；绩点 >= 1.5 */
class LectureCourse : public Course {
private:
    string classroom_;   // 教室
    bool openToAll_;     // 是否通识选修
public:
    LectureCourse(const string& id, const string& name, const string& instructor,
                  int credits, int capacity, const string& schedule,
                  double creditPrice, const string& classroom, bool openToAll)
        : Course(id, name, instructor, credits, capacity, schedule, creditPrice),
          classroom_(classroom), openToAll_(openToAll) {}

    const string& getClassroom() const { return classroom_; }

    string getTypeName() const override { return "理论课(讲授)"; }
    double computeTuition() const override { return getCredits() * getCreditPrice(); }
    bool checkEnrollment(const Student& stu) const override { return stu.getGpa() >= 1.5; }
    string requirementDesc() const override { return "绩点 >= 1.5"; }

    void displayDetail() const override {
        Course::displayDetail();
        cout << "上课教室 : " << classroom_
             << (openToAll_ ? "（通识选修）" : "（专业课）") << "\n";
    }
};

/** @brief 实验课：学费 = 学分 x 单价 + 材料费；绩点 >= 规定要求 */
class LabCourse : public Course {
private:
    double requiredGpa_;   // 最低绩点
    string labRoom_;       // 实验室
    double labFee_;        // 材料费
public:
    LabCourse(const string& id, const string& name, const string& instructor,
              int credits, int capacity, const string& schedule, double creditPrice,
              double requiredGpa, const string& labRoom, double labFee)
        : Course(id, name, instructor, credits, capacity, schedule, creditPrice),
          requiredGpa_(requiredGpa), labRoom_(labRoom), labFee_(labFee) {}

    double getRequiredGpa() const { return requiredGpa_; }

    string getTypeName() const override { return "实验课"; }
    double computeTuition() const override {
        return getCredits() * getCreditPrice() + labFee_;
    }
    bool checkEnrollment(const Student& stu) const override {
        return stu.getGpa() >= requiredGpa_;
    }
    string requirementDesc() const override {
        ostringstream oss;
        oss << "绩点 >= " << fixed << setprecision(1) << requiredGpa_
            << "，通过实验室安全培训";
        return oss.str();
    }
    void displayDetail() const override {
        Course::displayDetail();
        cout << "实验地点 : " << labRoom_ << "\n";
        cout << "材料费用 : " << fixed << setprecision(1) << labFee_ << " 元（已含在学费中）\n";
    }
};

/** @brief 在线课：学费 = 学分 x 单价 x 折扣 + 平台费；无附加门槛 */
class OnlineCourse : public Course {
private:
    string platform_;      // 平台
    double platformFee_;   // 平台服务费
    double discount_;      // 折扣系数
public:
    OnlineCourse(const string& id, const string& name, const string& instructor,
                 int credits, int capacity, const string& schedule, double creditPrice,
                 const string& platform, double platformFee, double discount)
        : Course(id, name, instructor, credits, capacity, schedule, creditPrice),
          platform_(platform), platformFee_(platformFee), discount_(discount) {}

    const string& getPlatform() const { return platform_; }

    string getTypeName() const override { return "在线课"; }
    double computeTuition() const override {
        return getCredits() * getCreditPrice() * discount_ + platformFee_;
    }
    bool checkEnrollment(const Student&) const override { return true; }
    string requirementDesc() const override {
        ostringstream oss;
        oss << "无附加要求，线上随到随学，享 " << fixed << setprecision(0)
            << discount_ * 10 << " 折优惠";
        return oss.str();
    }
    void displayDetail() const override {
        Course::displayDetail();
        cout << "授课平台 : " << platform_ << "\n";
        cout << "平台费用 : " << fixed << setprecision(1) << platformFee_ << " 元\n";
    }
};

/*============================== 报名管理部分 ==============================*/

/** @brief 一条报名记录（保留历史，退课后 active = false） */
struct Enrollment {
    int sn = 0;           // 报名流水号
    string studentId;     // 学号
    string courseId;      // 课程编号
    string date;          // 报名日期
    bool active = true;   // 是否有效
};

/**
 * @brief 报名管理类：报名业务的核心（选课、退课、改记录、删除人员、统计）
 */
class EnrollmentManager {
private:
    vector<Student> students_;    // 报名人员（值持有，可增删）
    vector<Course*> courses_;     // 开设课程（借用指针，静态数据）
    vector<Enrollment> records_;  // 报名记录（含已退课历史）
    int nextSn_;                  // 下一个流水号
public:
    EnrollmentManager() : nextSn_(1) {}

    /* ---- 数据装载（持久化恢复） ---- */
    void setAll(const vector<Student>& students, const vector<Enrollment>& records);
    void syncSequence();                        // 依已有记录重算流水号

    /* ---- 课程 ---- */
    bool addCourse(Course* course);
    Course* findCourse(const string& id);
    const Course* findCourse(const string& id) const;
    const vector<Course*>& getCourses() const { return courses_; }

    /* ---- 人员（报名人员的增删改） ---- */
    bool addStudent(const Student& s);
    bool removeStudent(const string& id, int& removedRecords);
    Student* findStudent(const string& id);
    const Student* findStudent(const string& id) const;
    const vector<Student>& getStudents() const { return students_; }
    int recordCountOf(const string& studentId) const;    // 全部记录数
    int activeCountOf(const string& studentId) const;    // 有效记录数

    /* ---- 记录查询 ---- */
    int countActive(const string& courseId) const;
    bool hasActiveRecord(const string& studentId, const string& courseId) const;
    Enrollment* findRecord(int sn);
    const Enrollment* findRecord(int sn) const;
    const vector<Enrollment>& getRecords() const { return records_; }

    /* ---- 核心业务 ---- */
    EnrollStatus enroll(const string& studentId, const string& courseId);
    EnrollStatus drop(const string& studentId, const string& courseId);
    EnrollStatus changeRecordCourse(int sn, const string& newCourseId);
    EnrollStatus setRecordActive(int sn, bool active);

    /* ---- 展示（操作后实时刷新界面） ---- */
    void showCourseCatalog() const;
    void showCourseInfo(const string& courseId) const;
    void showStudentEnrollments(const string& studentId) const;
    void showStatistics() const;
    void showAllRecords() const;
    void showPersons() const;
    string describeRecord(const Enrollment& rec) const;
};

/* -------------------- EnrollmentManager 成员函数实现 -------------------- */

void EnrollmentManager::setAll(const vector<Student>& students,
                               const vector<Enrollment>& records) {
    students_ = students;
    records_ = records;
    // 关键：根据“有效”报名记录重建每个学生的已选课程与学分，
    // 保证重启程序后统计信息与报名记录保持一致（数据真实持久化）
    for (const auto& rec : records_) {
        if (!rec.active) continue;
        Student* s = findStudent(rec.studentId);
        Course* c = findCourse(rec.courseId);
        if (s != nullptr && c != nullptr)
            s->addCourse(rec.courseId, c->getCredits());
    }
    syncSequence();
}

void EnrollmentManager::syncSequence() {
    int maxSn = 0;
    for (const auto& r : records_)
        if (r.sn > maxSn) maxSn = r.sn;
    nextSn_ = maxSn + 1;
}

bool EnrollmentManager::addCourse(Course* course) {
    if (course == nullptr || findCourse(course->getId()) != nullptr) return false;
    courses_.push_back(course);
    return true;
}

Course* EnrollmentManager::findCourse(const string& id) {
    for (Course* c : courses_)
        if (c != nullptr && c->getId() == id) return c;
    return nullptr;
}

const Course* EnrollmentManager::findCourse(const string& id) const {
    for (const Course* c : courses_)
        if (c != nullptr && c->getId() == id) return c;
    return nullptr;
}

bool EnrollmentManager::addStudent(const Student& s) {
    if (findStudent(s.getId()) != nullptr) return false;   // 学号唯一
    students_.push_back(s);
    return true;
}

bool EnrollmentManager::removeStudent(const string& id, int& removedRecords) {
    removedRecords = 0;
    for (auto it = students_.begin(); it != students_.end(); ++it) {
        if (it->getId() != id) continue;
        int cnt = 0;
        for (const auto& r : records_)
            if (r.studentId == id) ++cnt;
        records_.erase(remove_if(records_.begin(), records_.end(),
                                 [&id](const Enrollment& r) { return r.studentId == id; }),
                       records_.end());
        removedRecords = cnt;
        students_.erase(it);
        return true;
    }
    return false;
}

Student* EnrollmentManager::findStudent(const string& id) {
    for (auto& s : students_)
        if (s.getId() == id) return &s;
    return nullptr;
}

const Student* EnrollmentManager::findStudent(const string& id) const {
    for (const auto& s : students_)
        if (s.getId() == id) return &s;
    return nullptr;
}

int EnrollmentManager::recordCountOf(const string& studentId) const {
    int n = 0;
    for (const auto& r : records_)
        if (r.studentId == studentId) ++n;
    return n;
}

int EnrollmentManager::activeCountOf(const string& studentId) const {
    int n = 0;
    for (const auto& r : records_)
        if (r.active && r.studentId == studentId) ++n;
    return n;
}

int EnrollmentManager::countActive(const string& courseId) const {
    int n = 0;
    for (const auto& r : records_)
        if (r.active && r.courseId == courseId) ++n;
    return n;
}

bool EnrollmentManager::hasActiveRecord(const string& studentId,
                                         const string& courseId) const {
    for (const auto& r : records_)
        if (r.active && r.studentId == studentId && r.courseId == courseId) return true;
    return false;
}

Enrollment* EnrollmentManager::findRecord(int sn) {
    for (auto& r : records_)
        if (r.sn == sn) return &r;
    return nullptr;
}

const Enrollment* EnrollmentManager::findRecord(int sn) const {
    for (const auto& r : records_)
        if (r.sn == sn) return &r;
    return nullptr;
}

/**
 * @brief 选课（按顺序处理各类边界情况，全部通过才落库）
 */
EnrollStatus EnrollmentManager::enroll(const string& studentId, const string& courseId) {
    Student* stu = findStudent(studentId);
    if (stu == nullptr) return EnrollStatus::StudentNotFound;
    Course* course = findCourse(courseId);
    if (course == nullptr) return EnrollStatus::CourseNotFound;
    if (hasActiveRecord(studentId, courseId)) return EnrollStatus::AlreadyEnrolled;
    if (countActive(courseId) >= course->getCapacity()) return EnrollStatus::CourseFull;
    if (!course->checkEnrollment(*stu)) return EnrollStatus::EligibilityNotMet;

    Enrollment rec;
    rec.sn = nextSn_++;
    rec.studentId = studentId;
    rec.courseId = courseId;
    rec.date = currentDateString();
    rec.active = true;
    records_.push_back(rec);
    stu->addCourse(courseId, course->getCredits());
    return EnrollStatus::Success;
}

/**
 * @brief 退课：将记录置为无效并同步学生学分（保留历史）
 */
EnrollStatus EnrollmentManager::drop(const string& studentId, const string& courseId) {
    Student* stu = findStudent(studentId);
    if (stu == nullptr) return EnrollStatus::StudentNotFound;
    Course* course = findCourse(courseId);
    if (course == nullptr) return EnrollStatus::CourseNotFound;

    for (auto& rec : records_) {
        if (rec.active && rec.studentId == studentId && rec.courseId == courseId) {
            rec.active = false;
            stu->removeCourse(courseId, course->getCredits());
            return EnrollStatus::Success;
        }
    }
    return EnrollStatus::NotEnrolled;
}

/**
 * @brief 修改报名记录：更换选修课程（等价于“退旧课 + 选新课”的原子操作）
 */
EnrollStatus EnrollmentManager::changeRecordCourse(int sn, const string& newCourseId) {
    Enrollment* rec = findRecord(sn);
    if (rec == nullptr) return EnrollStatus::RecordNotFound;
    if (!rec->active) return EnrollStatus::NotEnrolled;
    if (rec->courseId == newCourseId) return EnrollStatus::SameCourse;

    Student* stu = findStudent(rec->studentId);
    if (stu == nullptr) return EnrollStatus::StudentNotFound;
    Course* newCourse = findCourse(newCourseId);
    if (newCourse == nullptr) return EnrollStatus::CourseNotFound;
    if (hasActiveRecord(rec->studentId, newCourseId)) return EnrollStatus::AlreadyEnrolled;
    if (countActive(newCourseId) >= newCourse->getCapacity()) return EnrollStatus::CourseFull;
    if (!newCourse->checkEnrollment(*stu)) return EnrollStatus::EligibilityNotMet;

    Course* oldCourse = findCourse(rec->courseId);   // 旧课学分扣减（防御性判空）
    stu->removeCourse(rec->courseId, oldCourse != nullptr ? oldCourse->getCredits() : 0);
    rec->courseId = newCourseId;
    stu->addCourse(newCourseId, newCourse->getCredits());
    return EnrollStatus::Success;
}

/**
 * @brief 修改报名记录状态：true = 恢复有效（重新选课校验），false = 置为退课
 */
EnrollStatus EnrollmentManager::setRecordActive(int sn, bool active) {
    Enrollment* rec = findRecord(sn);
    if (rec == nullptr) return EnrollStatus::RecordNotFound;
    if (rec->active == active) return active ? EnrollStatus::AlreadyEnrolled
                                              : EnrollStatus::NotEnrolled;

    Student* stu = findStudent(rec->studentId);
    if (stu == nullptr) return EnrollStatus::StudentNotFound;
    Course* course = findCourse(rec->courseId);
    if (course == nullptr) return EnrollStatus::CourseNotFound;

    if (active) {   // 恢复有效：按选课规则重新校验
        if (hasActiveRecord(rec->studentId, rec->courseId)) return EnrollStatus::AlreadyEnrolled;
        if (countActive(rec->courseId) >= course->getCapacity()) return EnrollStatus::CourseFull;
        if (!course->checkEnrollment(*stu)) return EnrollStatus::EligibilityNotMet;
        rec->active = true;
        stu->addCourse(rec->courseId, course->getCredits());
    } else {        // 置为退课
        rec->active = false;
        stu->removeCourse(rec->courseId, course->getCredits());
    }
    return EnrollStatus::Success;
}

void EnrollmentManager::showCourseCatalog() const {
    cout << "  课程清单（类型 / 学费 / 报名人数均实时统计）：\n";
    for (const Course* c : courses_) {
        cout << "    " << c->getId() << "  " << c->getName()
             << " | 类型:" << c->getTypeName()
             << " | 学分:" << c->getCredits()
             << " | 容量:" << c->getCapacity()
             << " | 学费:" << fixed << setprecision(1) << c->computeTuition() << " 元"
             << " | 已报:" << countActive(c->getId()) << " 人\n";
    }
}

void EnrollmentManager::showCourseInfo(const string& courseId) const {
    const Course* c = findCourse(courseId);
    if (c == nullptr) {
        cout << "  [失败] 查询课程详情：课程编号 " << courseId << " 不存在！\n";
        return;
    }
    c->displayDetail();                       // 运行期多态
    int n = countActive(courseId);
    cout << "当前报名 : " << n << " 人（剩余 "
         << max(0, c->getCapacity() - n) << " 个名额）\n";
    cout << "--------------------------------------------------\n";
}

void EnrollmentManager::showStudentEnrollments(const string& studentId) const {
    const Student* stu = findStudent(studentId);
    if (stu == nullptr) {
        cout << "  [失败] 学生 " << studentId << " 不存在！\n";
        return;
    }
    cout << "  " << stu->getId() << " " << stu->getName()
         << "（" << stu->getMajor() << ", 绩点 " << fixed << setprecision(2)
         << stu->getGpa() << "）报名清单：\n";

    int count = 0;
    for (const auto& rec : records_) {
        if (rec.studentId != studentId || !rec.active) continue;
        const Course* c = findCourse(rec.courseId);
        if (c == nullptr) continue;
        cout << "    #" << rec.sn << " - " << c->getId() << " " << c->getName()
             << " (" << c->getTypeName() << ") " << c->getCredits() << " 学分"
             << " | 学费 " << fixed << setprecision(1) << c->computeTuition()
             << " 元 | " << rec.date << "\n";
        ++count;
    }
    if (count == 0) cout << "    （暂无有效报名记录）\n";
    cout << "    合计：" << count << " 门 / " << stu->getSelectedCredits() << " 学分\n";
}

string EnrollmentManager::describeRecord(const Enrollment& rec) const {
    const Student* s = findStudent(rec.studentId);
    const Course* c = findCourse(rec.courseId);
    ostringstream oss;
    oss << "#" << rec.sn << "  " << (s != nullptr ? s->getName() : rec.studentId)
        << "(" << rec.studentId << ") -> "
        << (c != nullptr ? c->getName() : rec.courseId)
        << "(" << rec.courseId << ")  ["
        << (rec.active ? "有效" : "已退课") << "]  " << rec.date;
    return oss.str();
}

void EnrollmentManager::showPersons() const {
    cout << "  当前报名人员共 " << students_.size() << " 人：\n";
    for (const Student& s : students_) {
        cout << "    " << s.getId() << " " << s.getName()
             << " | " << s.getMajor()
             << " | 绩点 " << fixed << setprecision(2) << s.getGpa()
             << " | " << s.getPhone()
             << " | 有效报名 " << activeCountOf(s.getId()) << " 门\n";
    }
}

void EnrollmentManager::showStatistics() const {
    cout << "\n  [1] 各课程报名人数：\n";
    for (const Course* c : courses_) {
        int n = countActive(c->getId());
        int remain = c->getCapacity() - n;
        double rate = c->getCapacity() > 0 ? 100.0 * n / c->getCapacity() : 0.0;
        cout << "    " << c->getId() << " " << c->getName()
             << " (" << c->getTypeName() << ")"
             << " : 已报 " << n << " / " << c->getCapacity() << " 人"
             << ", 剩余 " << remain << " 个名额"
             << ", 占用率 " << fixed << setprecision(1) << rate << "%";
        if (remain <= 0) cout << "  [满员]";
        cout << "\n";
    }

    map<string, int> courseByType, enrollByType;
    for (const Course* c : courses_) {
        courseByType[c->getTypeName()] += 1;
        enrollByType[c->getTypeName()] += countActive(c->getId());
    }
    cout << "\n  [2] 按课程类型汇总：\n";
    for (const auto& kv : courseByType)
        cout << "    " << kv.first << " : " << kv.second << " 门课程, "
             << enrollByType[kv.first] << " 人次报名\n";

    cout << "\n  [3] 人员选课学分统计：\n";
    for (const Student& s : students_)
        cout << "    " << s.getId() << " " << s.getName()
             << " : 已选 " << s.getSelectedIds().size() << " 门, 共 "
             << s.getSelectedCredits() << " 学分\n";

    int activeCount = 0, inactiveCount = 0;
    for (const auto& r : records_) {
        if (r.active) ++activeCount;
        else ++inactiveCount;
    }
    cout << "\n  [4] 报名记录汇总：有效 " << activeCount << " 条, 已退课 "
         << inactiveCount << " 条, 历史合计 " << records_.size() << " 条。\n";

    cout << "  [5] 满员课程：";
    bool anyFull = false;
    for (const Course* c : courses_) {
        if (countActive(c->getId()) >= c->getCapacity()) {
            cout << c->getId() << " " << c->getName() << "  ";
            anyFull = true;
        }
    }
    if (!anyFull) cout << "无";
    cout << "\n";
}

void EnrollmentManager::showAllRecords() const {
    cout << "\n  报名流水明细（共 " << records_.size() << " 条）：\n";
    if (records_.empty()) {
        cout << "    （暂无记录）\n";
        return;
    }
    for (const auto& rec : records_)
        cout << "    " << describeRecord(rec) << "\n";
}

/*============================ 账户与鉴权部分 =============================*/

/** @brief 一个登录账户 */
struct UserAccount {
    string username;    // 登录用户名（唯一）
    string name;        // 显示姓名
    string personId;    // 关联学号（管理员为空）
    string role;        // "admin" 管理员 | "student" 学生
    string passHash;    // 口令哈希（不保存明文）
};

/**
 * @brief 账户管理类：注册、鉴权、改密、按关联学号维护账户
 */
class AccountManager {
private:
    vector<UserAccount> users_;
public:
    void setAll(const vector<UserAccount>& users) { users_ = users; }
    const vector<UserAccount>& getUsers() const { return users_; }

    bool hasUsername(const string& username) const {
        for (const auto& u : users_)
            if (u.username == username) return true;
        return false;
    }

    bool hasPersonId(const string& personId) const {
        if (personId.empty()) return false;
        for (const auto& u : users_)
            if (u.personId == personId) return true;
        return false;
    }

    UserAccount* findByUsername(const string& username) {
        for (auto& u : users_)
            if (u.username == username) return &u;
        return nullptr;
    }

    const UserAccount* findByUsername(const string& username) const {
        for (const auto& u : users_)
            if (u.username == username) return &u;
        return nullptr;
    }

    bool addUser(const UserAccount& acc) {
        if (acc.username.empty() || hasUsername(acc.username)) return false;
        users_.push_back(acc);
        return true;
    }

    /** @brief 删除与某学号关联的账户，removedUsername 返回被删用户名 */
    bool removeByPersonId(const string& personId, string& removedUsername) {
        for (auto it = users_.begin(); it != users_.end(); ++it) {
            if (it->personId == personId && !it->personId.empty()) {
                removedUsername = it->username;
                users_.erase(it);
                return true;
            }
        }
        return false;
    }

    /** @brief 登录鉴权；失败时 reason 返回中文原因 */
    bool authenticate(const string& username, const string& password,
                      string& reason) const {
        const UserAccount* u = findByUsername(username);
        if (u == nullptr) { reason = "用户名不存在"; return false; }
        if (u->passHash != hashPassword(password)) { reason = "密码错误"; return false; }
        return true;
    }

    void setPassword(const string& username, const string& password) {
        UserAccount* u = findByUsername(username);
        if (u != nullptr) u->passHash = hashPassword(password);
    }

    /** @brief 人员改名后同步账户显示名 */
    void renameByPersonId(const string& personId, const string& newName) {
        for (auto& u : users_)
            if (u.personId == personId && !u.personId.empty()) u.name = newName;
    }
};

/*============================== 持久化部分 ===============================*/

/** @brief 一次完整的数据快照（用于装载） */
struct Snapshot {
    vector<UserAccount> users;
    vector<Student> students;
    vector<Enrollment> records;
    bool fileExists = false;
};

/**
 * @brief 存储类：把账户 / 人员 / 报名记录写入 system_data.txt（分节管道格式）
 *
 * 文件格式（UTF-8/GBK 均可，按写入时的本地编码原样保存）：
 *   [USERS]     username|name|personId|role|passHash
 *   [STUDENTS]  id|name|major|gpa|phone
 *   [RECORDS]   sn|studentId|courseId|date|active
 */
class Storage {
public:
    static string dataFilePath() { return "system_data.txt"; }

    /** @brief 原子化落盘：先写临时文件，再替换正式文件 */
    static bool saveAll(const vector<UserAccount>& users,
                        const vector<Student>& students,
                        const vector<Enrollment>& records) {
        const string path = dataFilePath();
        const string tmp = path + ".tmp";
        ofstream out(tmp.c_str(), ios::binary | ios::trunc);
        if (!out) return false;

        out << "# 课程报名管理系统数据文件（自动生成，请勿手工编辑）\n";
        out << "[USERS]\n";
        for (const auto& u : users)
            out << u.username << '|' << u.name << '|' << u.personId << '|'
                << u.role << '|' << u.passHash << '\n';
        out << "[STUDENTS]\n";
        for (const auto& s : students)
            out << s.getId() << '|' << s.getName() << '|' << s.getMajor() << '|'
                << fixed << setprecision(2) << s.getGpa() << '|' << s.getPhone() << '\n';
        out << "[RECORDS]\n";
        for (const auto& r : records)
            out << r.sn << '|' << r.studentId << '|' << r.courseId << '|'
                << r.date << '|' << (r.active ? 1 : 0) << '\n';
        out.close();
        if (!out) { remove(tmp.c_str()); return false; }

        remove(path.c_str());                       // Windows 下 rename 前需先删除目标
        if (rename(tmp.c_str(), path.c_str()) != 0) { remove(tmp.c_str()); return false; }
        return true;
    }

    /** @brief 读取数据文件；文件不存在时返回 fileExists = false */
    static Snapshot load() {
        Snapshot snap;
        ifstream in(dataFilePath().c_str(), ios::binary);
        if (!in) return snap;                       // 首次运行
        snap.fileExists = true;

        string line, section;
        while (getline(in, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                continue;
            }
            vector<string> f = split(line, '|');
            if (section == "USERS" && f.size() == 5) {
                UserAccount u;
                u.username = f[0]; u.name = f[1]; u.personId = f[2];
                u.role = f[3]; u.passHash = f[4];
                snap.users.push_back(u);
            } else if (section == "STUDENTS" && f.size() == 5) {
                double gpa = 0.0;
                try { gpa = stod(f[3]); } catch (...) { gpa = 0.0; }
                snap.students.push_back(Student(f[0], f[1], f[2], gpa, f[4]));
            } else if (section == "RECORDS" && f.size() == 5) {
                Enrollment r;
                try { r.sn = stoi(f[0]); } catch (...) { r.sn = 0; }
                r.studentId = f[1];
                r.courseId = f[2];
                r.date = f[3];
                r.active = (f[4] == "1");
                snap.records.push_back(r);
            }
        }
        return snap;
    }
};

/*=============================== 界面与交互 ===============================*/

/**
 * @brief 系统界面类：菜单渲染、输入读取与校验、权限门禁、操作反馈、持久化触发
 */
class SystemUI {
private:
    EnrollmentManager manager_;              // 报名业务核心
    AccountManager accounts_;                // 账户与鉴权
    vector<Course*> courses_;                // 课程（main 中创建，借用）
    vector<Person*> teachers_;               // 教师（main 中创建，借用，演示多态）
    const UserAccount* currentUser_ = nullptr;   // 当前登录账户

public:
    SystemUI(const vector<Course*>& courses, const vector<Person*>& teachers)
        : courses_(courses), teachers_(teachers) {}

    void init();     // 加载或初始化数据
    void run();      // 菜单主循环

private:
    /* ---------- 输入辅助 ---------- */
    string readLine(const string& prompt);                       // EOF 时保存并退出
    string ask(const string& label,
               const function<bool(const string&, string&)>& v,
               bool allowEmpty = false);
    bool confirm(const string& question);
    void pause();

    /* ---------- 权限门禁 ---------- */
    bool requireLogin(const string& action);
    bool requireStudent(const string& action);
    bool requireAdmin(const string& action);

    /* ---------- 持久化 ---------- */
    bool persist();

    /* ---------- 显示 ---------- */
    void printHeader() const;
    void printMenu() const;

    /* ---------- 菜单功能 ---------- */
    void browseCatalog();
    void queryCourse();
    void doRegister();
    void doLogin();
    void doEnroll();
    void doDrop();
    void showMyEnrollments();
    void showMyProfile();
    void adminAddPerson();
    void adminQueryRecords();
    void adminUpdateRecord();
    void adminUpdatePerson();
    void adminDeletePerson();
    void adminStatistics();
    void adminAllRecords();
    void changePassword();
    void doLogout();
};

/* -------------------- 输入辅助 -------------------- */

/**
 * @brief 读取一行输入；输入流结束（如直接关闭）时先保存数据再退出
 */
string SystemUI::readLine(const string& prompt) {
    cout << prompt;
    cout.flush();
    string line;
    if (!getline(cin, line)) {
        Storage::saveAll(accounts_.getUsers(), manager_.getStudents(),
                         manager_.getRecords());
        cout << "\n  [系统] 输入已结束，数据已自动保存，程序退出。\n";
        exit(0);
    }
    return line;
}

/**
 * @brief 表单项读取：循环直到校验通过；allowEmpty 时允许直接回车返回空串
 */
string SystemUI::ask(const string& label,
                     const function<bool(const string&, string&)>& v,
                     bool allowEmpty) {
    while (true) {
        string s = trim(readLine("  " + label + " : "));
        if (s.empty() && allowEmpty) return s;
        string err;
        if (v(s, err)) return s;
        cout << "  [输入错误] " << err << "，请重新输入。\n";
    }
}

/**
 * @brief 二次确认：y/是 与 n/否，其他输入给出错误提示并重试
 */
bool SystemUI::confirm(const string& question) {
    while (true) {
        string s = trim(readLine("  " + question + " [y/n] : "));
        if (s.empty()) { cout << "  [输入错误] 请输入 y（是）或 n（否）。\n"; continue; }
        char c = (char)tolower((unsigned char)s[0]);
        if (c == 'y') return true;
        if (c == 'n') return false;
        cout << "  [输入错误] 请输入 y（是）或 n（否）。\n";
    }
}

void SystemUI::pause() {
    readLine("\n  按 Enter 键返回菜单 ... ");
}

/* -------------------- 权限门禁（需求 1：未登录不可报名） -------------------- */

bool SystemUI::requireLogin(const string& action) {
    if (currentUser_ == nullptr) {
        cout << "  [提示] 尚未登录，无法进行「" << action
             << "」，请先使用菜单 4 登录！\n";
        return false;
    }
    return true;
}

bool SystemUI::requireStudent(const string& action) {
    if (!requireLogin(action)) return false;
    if (currentUser_->role == "admin") {
        cout << "  [提示] 管理员账户不参与报名，请使用学生账户进行「"
             << action << "」。\n";
        return false;
    }
    if (manager_.findStudent(currentUser_->personId) == nullptr) {
        cout << "  [错误] 账户关联的报名人员不存在（可能已被删除），请联系管理员。\n";
        return false;
    }
    return true;
}

bool SystemUI::requireAdmin(const string& action) {
    if (!requireLogin(action)) return false;
    if (currentUser_->role != "admin") {
        cout << "  [权限不足] 「" << action << "」仅管理员可用。\n";
        return false;
    }
    return true;
}

/* -------------------- 持久化（需求 3：数据真实保存） -------------------- */

bool SystemUI::persist() {
    bool ok = Storage::saveAll(accounts_.getUsers(), manager_.getStudents(),
                               manager_.getRecords());
    if (ok) cout << "  [已保存] 数据已写入 " << Storage::dataFilePath() << "\n";
    else cout << "  [警告] 数据保存失败！文件可能被占用或无写入权限。\n";
    return ok;
}

/* -------------------- 界面渲染 -------------------- */

void SystemUI::printHeader() const {
    cout << "\n================================================================\n";
    cout << "                  课程报名管理系统  v2.0\n";
    cout << "----------------------------------------------------------------\n";
    if (currentUser_ == nullptr) {
        cout << " 当前状态：未登录（访客）—— 报名 / 退课 / 管理功能均需登录\n";
    } else {
        string roleName = currentUser_->role == "admin" ? "管理员" : "学生";
        cout << " 当前状态：已登录  " << currentUser_->name << "（" << roleName;
        if (currentUser_->role == "student")
            cout << ", 学号 " << currentUser_->personId;
        cout << "）  账户：" << currentUser_->username << "\n";
    }
    cout << "----------------------------------------------------------------\n";
}

void SystemUI::printMenu() const {
    cout << "  【基础】  1. 浏览课程目录与师资     2. 查询课程详情\n";
    cout << "            3. 用户注册                4. 用户登录\n";
    cout << "  【报名】* 需学生账户登录后使用\n";
    cout << "            5. 课程报名（选课）        6. 退课\n";
    cout << "            7. 查询我的报名信息        8. 查看我的个人信息\n";
    cout << "  【管理】* 仅管理员可用\n";
    cout << "            a. 添加报名人员            b. 按条件查询报名信息\n";
    cout << "            c. 修改报名记录            d. 修改人员信息\n";
    cout << "            e. 删除报名人员            f. 报名统计报表\n";
    cout << "            g. 查看全部报名流水\n";
    cout << "  【账户】  m. 修改登录密码            l. 注销登录\n";
    cout << "            0. 退出系统（自动保存数据）\n";
    cout << "================================================================\n";
}

/* -------------------- 初始化与主循环 -------------------- */

void SystemUI::init() {
    for (Course* c : courses_) manager_.addCourse(c);   // 装载静态课程

    Snapshot snap = Storage::load();
    accounts_.setAll(snap.users);
    manager_.setAll(snap.students, snap.records);

    cout << "\n================================================================\n";
    cout << "                  欢迎使用课程报名管理系统\n";
    cout << "================================================================\n";

    if (!snap.fileExists) {
        // 首次运行：写入默认管理员与示例报名人员
        accounts_.addUser(UserAccount{ "admin", "系统管理员", "",
                                       "admin", hashPassword("admin123") });
        manager_.addStudent(Student("20240101", "张三", "计算机科学与技术",
                                    3.8, "13800000001"));
        manager_.addStudent(Student("20240102", "李四", "软件工程",
                                    2.2, "13800000002"));
        manager_.addStudent(Student("20240103", "王五", "数据科学与大数据技术",
                                    4.1, "13800000003"));
        persist();
        cout << "  [初始化] 首次运行：已创建默认管理员 admin / admin123；\n";
        cout << "           已录入 3 名示例报名人员（20240101 张三、20240102 李四、"
                "20240103 王五），尚无登录账户，可在【3 用户注册】以学号绑定。\n";
    } else if (accounts_.getUsers().empty()) {
        accounts_.addUser(UserAccount{ "admin", "系统管理员", "",
                                       "admin", hashPassword("admin123") });
        persist();
        cout << "  [提示] 数据文件中没有可用账户，已重建默认管理员 admin / admin123。\n";
    }

    cout << "  [数据] 已从 " << Storage::dataFilePath() << " 加载：账户 "
         << accounts_.getUsers().size() << " 个，报名人员 "
         << manager_.getStudents().size() << " 人，报名记录 "
         << manager_.getRecords().size() << " 条。\n";
    cout << "================================================================\n";
}

void SystemUI::run() {
    bool running = true;
    while (running) {
        printHeader();
        printMenu();
        string choice = trim(readLine(" 请选择操作 > "));
        if (choice.empty()) {
            cout << "  [错误] 输入为空，请输入有效的菜单项！\n";
            pause();
            continue;
        }

        bool doPause = true;
        if (choice.size() != 1) {
            cout << "  [错误] 无效的菜单选项：[" << choice
                 << "]，请只输入单个菜单项（如 1 / a / 0）。\n";
        } else {
            char c = choice[0];
            if (isalpha((unsigned char)c)) c = (char)tolower((unsigned char)c);
            switch (c) {
            case '1': browseCatalog();          break;
            case '2': queryCourse();            break;
            case '3': doRegister();             break;
            case '4': doLogin();                break;
            case '5': doEnroll();               break;
            case '6': doDrop();                 break;
            case '7': showMyEnrollments();      break;
            case '8': showMyProfile();          break;
            case 'a': adminAddPerson();         break;
            case 'b': adminQueryRecords();      break;
            case 'c': adminUpdateRecord();      break;
            case 'd': adminUpdatePerson();      break;
            case 'e': adminDeletePerson();      break;
            case 'f': adminStatistics();        break;
            case 'g': adminAllRecords();        break;
            case 'm': changePassword();         break;
            case 'l': doLogout();               break;
            case '0': running = false; doPause = false; break;
            default:
                cout << "  [错误] 无效的菜单选项：'" << choice
                     << "'，请查看菜单重新输入。\n";
                break;
            }
        }
        if (running && doPause) pause();
    }

    persist();
    cout << "\n  感谢使用课程报名管理系统，再见！\n";
}

/* -------------------- 基础功能 -------------------- */

void SystemUI::browseCatalog() {
    cout << "\n--- 课程目录与师资（Person* 调用虚函数，体现多态）---\n";
    cout << "  【师资队伍】\n";
    for (Person* p : teachers_) {
        cout << "    ";
        p->displayBrief();
        cout << "\n";
    }
    cout << "  【课程目录】\n";
    manager_.showCourseCatalog();
}

void SystemUI::queryCourse() {
    cout << "\n--- 查询课程详情 ---\n";
    string cid = ask("请输入课程编号（直接回车取消）", vRequired, true);
    if (cid.empty()) { cout << "  [提示] 已取消查询。\n"; return; }
    manager_.showCourseInfo(cid);       // 内部处理“课程不存在”
}

/* -------------------- 账户功能（需求 1） -------------------- */

void SystemUI::doRegister() {
    cout << "\n--- 用户注册（报名人员账户）---\n";
    if (currentUser_ != nullptr) {
        cout << "  [提示] 已登录为 " << currentUser_->username
             << "，注册新账户前请先注销（菜单 l）。\n";
        return;
    }

    string pid = ask("学号/工号（4-16 位字母数字，直接回车取消）", vPersonId, true);
    if (pid.empty()) { cout << "  [提示] 已取消注册。\n"; return; }

    if (accounts_.hasPersonId(pid)) {
        cout << "  [失败] 学号 " << pid << " 已绑定登录账户，请直接登录。\n";
        return;
    }

    Student* existing = manager_.findStudent(pid);
    bool personExists = (existing != nullptr);
    string displayName;
    string name, major, gpaStr, phone;

    if (personExists) {
        displayName = existing->getName();
        cout << "  [提示] 已存在报名人员：" << existing->getName()
             << "（" << existing->getMajor() << "，联系电话 "
             << existing->getPhone() << "）。\n";
        if (!confirm("是否直接绑定该人员信息完成注册")) {
            cout << "  [提示] 已取消注册。\n";
            return;
        }
    } else {
        // 新人员：采集基本信息（含姓名、专业、绩点、联系方式），暂不落库
        cout << "  >> 该学号尚未登记，请填写报名人员基本信息：\n";
        name = ask("姓名", vName);
        major = ask("专业", vMajor);
        gpaStr = ask("绩点（0.00 - 5.00）", vGpa);
        phone = ask("联系电话（7-15 位数字，可含短横线）", vPhone);
        displayName = name;
    }

    // 用户名（唯一性 + 格式）
    string username;
    while (true) {
        username = ask("设置登录用户名（3-16 位字母/数字/下划线）", vUsername);
        if (accounts_.hasUsername(username)) {
            cout << "  [失败] 用户名 [" << username << "] 已被占用，请换一个。\n";
            continue;
        }
        break;
    }

    // 密码 + 二次确认
    string pwd1 = ask("设置登录密码（至少 6 位）", vPassword);
    string pwd2 = ask("确认登录密码", vPassword);
    while (pwd1 != pwd2) {
        cout << "  [输入错误] 两次输入的密码不一致，请重新设置。\n";
        pwd1 = ask("设置登录密码（至少 6 位）", vPassword);
        pwd2 = ask("确认登录密码", vPassword);
    }

    // 全部输入校验通过后，再统一创建人员与账户（避免脏数据）
    if (!personExists)
        manager_.addStudent(Student(pid, name, major, stod(gpaStr), phone));

    UserAccount acc;
    acc.username = username;
    acc.name = displayName;
    acc.personId = pid;
    acc.role = "student";
    acc.passHash = hashPassword(pwd1);
    if (!accounts_.addUser(acc)) {
        cout << "  [失败] 注册失败：用户名创建冲突。\n";
        if (!personExists) {    // 回滚刚创建的人员，保证数据一致
            int removed = 0;
            manager_.removeStudent(pid, removed);
        }
        return;
    }
    persist();
    cout << "  [成功] 注册成功！账户：" << username
         << "，关联学号：" << pid << "（" << displayName << "）\n";
    cout << "  [提示] 请使用菜单 4 登录后进行课程报名。\n";
}

void SystemUI::doLogin() {
    cout << "\n--- 用户登录 ---\n";
    if (currentUser_ != nullptr) {
        cout << "  [提示] 已登录为 " << currentUser_->username
             << "，如需切换账户请先注销（菜单 l）。\n";
        return;
    }
    string username = ask("用户名（直接回车取消）", vRequired, true);
    if (username.empty()) { cout << "  [提示] 已取消登录。\n"; return; }
    string password = ask("密码", vRequired);

    string reason;
    if (!accounts_.authenticate(username, password, reason)) {
        cout << "  [失败] 登录失败：" << reason << "，请检查后重试。\n";
        return;
    }
    currentUser_ = accounts_.findByUsername(username);
    cout << "  [成功] 登录成功，欢迎 " << currentUser_->name << "！角色："
         << (currentUser_->role == "admin" ? "管理员" : "学生") << "\n";
    if (currentUser_->role == "student")
        cout << "  [提示] 关联学号：" << currentUser_->personId
             << "，现在可以进行课程报名了。\n";
}

void SystemUI::doLogout() {
    cout << "\n--- 注销登录 ---\n";
    if (currentUser_ == nullptr) {
        cout << "  [提示] 当前未登录，无需注销。\n";
        return;
    }
    cout << "  [成功] 账户 " << currentUser_->username << " 已注销。\n";
    currentUser_ = nullptr;
    cout << "  [提示] 当前为未登录状态，报名类操作将被禁止。\n";
}

void SystemUI::changePassword() {
    cout << "\n--- 修改登录密码 ---\n";
    if (!requireLogin("修改密码")) return;

    string oldPwd = ask("当前密码", vRequired);
    string reason;
    if (!accounts_.authenticate(currentUser_->username, oldPwd, reason)) {
        cout << "  [失败] 修改失败：当前密码错误。\n";
        return;
    }
    string newPwd = ask("新密码（至少 6 位）", vPassword);
    string newPwd2 = ask("确认新密码", vPassword);
    if (newPwd != newPwd2) {
        cout << "  [失败] 两次输入的新密码不一致，本次修改已取消。\n";
        return;
    }
    accounts_.setPassword(currentUser_->username, newPwd);
    persist();
    cout << "  [成功] 密码修改成功，下次登录请使用新密码。\n";
}

/* -------------------- 报名功能（需求 1 + 2） -------------------- */

void SystemUI::doEnroll() {
    cout << "\n--- 课程报名（选课）---\n";
    if (!requireStudent("课程报名")) return;

    manager_.showCourseCatalog();
    string cid = ask("请输入要报名的课程编号（直接回车取消）", vRequired, true);
    if (cid.empty()) { cout << "  [提示] 已取消报名。\n"; return; }

    EnrollStatus st = manager_.enroll(currentUser_->personId, cid);
    if (st == EnrollStatus::Success) {
        cout << "  [成功] 课程报名成功！\n";
        persist();
        cout << "  ---- 界面刷新：我的最新报名信息 ----\n";
        manager_.showStudentEnrollments(currentUser_->personId);   // 实时更新
    } else {
        cout << "  [失败] 课程报名失败：" << enrollStatusText(st) << "\n";
    }
}

void SystemUI::doDrop() {
    cout << "\n--- 退课 ---\n";
    if (!requireStudent("退课")) return;

    cout << "  ---- 当前有效报名 ----\n";
    manager_.showStudentEnrollments(currentUser_->personId);
    string cid = ask("请输入要退课的课程编号（直接回车取消）", vRequired, true);
    if (cid.empty()) { cout << "  [提示] 已取消退课。\n"; return; }

    EnrollStatus st = manager_.drop(currentUser_->personId, cid);
    if (st == EnrollStatus::Success) {
        cout << "  [成功] 退课成功，名额已释放！\n";
        persist();
        cout << "  ---- 界面刷新：退课后的最新报名信息 ----\n";
        manager_.showStudentEnrollments(currentUser_->personId);   // 实时更新
    } else {
        cout << "  [失败] 退课失败：" << enrollStatusText(st) << "\n";
    }
}

void SystemUI::showMyEnrollments() {
    cout << "\n--- 我的报名信息 ---\n";
    if (!requireStudent("查询我的报名信息")) return;
    manager_.showStudentEnrollments(currentUser_->personId);
}

void SystemUI::showMyProfile() {
    cout << "\n--- 我的个人信息 ---\n";
    if (!requireStudent("查看个人信息")) return;
    Student* s = manager_.findStudent(currentUser_->personId);
    if (s != nullptr) s->displayInfo();                       // 多态展示
    cout << "  登录账户：" << currentUser_->username
         << "（" << (currentUser_->role == "admin" ? "管理员" : "学生") << "）\n";
}

/* -------------------- 管理功能（需求 2） -------------------- */

void SystemUI::adminAddPerson() {
    cout << "\n--- 添加报名人员 ---\n";
    if (!requireAdmin("添加报名人员")) return;

    string pid = ask("学号/工号（4-16 位字母数字）", vPersonId);
    if (manager_.findStudent(pid) != nullptr) {
        cout << "  [失败] 学号 " << pid << " 已存在，不能重复添加。\n";
        return;
    }
    string name = ask("姓名", vName);
    string major = ask("专业", vMajor);
    string gpaStr = ask("绩点（0.00 - 5.00）", vGpa);
    string phone = ask("联系电话（7-15 位数字，可含短横线）", vPhone);

    if (!manager_.addStudent(Student(pid, name, major, stod(gpaStr), phone))) {
        cout << "  [失败] 添加报名人员失败。\n";
        return;
    }
    persist();
    cout << "  [成功] 报名人员添加成功！当前共 "
         << manager_.getStudents().size() << " 人。\n";
    cout << "  ---- 界面刷新：新增人员档案 ----\n";
    Student* s = manager_.findStudent(pid);
    if (s != nullptr) s->displayInfo();
    cout << "  [提示] 可告知该人员使用【3 用户注册】以该学号绑定登录账户。\n";
}

void SystemUI::adminQueryRecords() {
    cout << "\n--- 按条件查询报名信息 ---\n";
    if (!requireAdmin("按条件查询报名信息")) return;

    cout << "  可用条件：\n";
    cout << "    1) 全部记录          2) 按学号              3) 按学生姓名（模糊）\n";
    cout << "    4) 按课程编号        5) 按课程名称（模糊）  6) 按状态\n";
    cout << "    7) 按课程类型（输入：理论 / 实验 / 在线）\n";

    string cond = ask("请选择查询条件编号（1-7，回车取消）", vRequired, true);
    if (cond.empty()) { cout << "  [提示] 已取消查询。\n"; return; }
    if (cond != "1" && cond != "2" && cond != "3" && cond != "4" &&
        cond != "5" && cond != "6" && cond != "7") {
        cout << "  [错误] 无效的查询条件：" << cond << "，请重新选择（1-7）。\n";
        return;
    }

    string value;
    bool needValue = (cond != "1");
    if (needValue && cond != "6") {
        value = ask("请输入查询值", vRequired);
    }

    // 状态条件需要进一步选择
    bool wantActive = true;
    if (cond == "6") {
        cout << "    1) 仅有效记录   2) 仅已退课记录\n";
        string sub = ask("请选择状态（1/2）", vRequired);
        wantActive = (sub != "2");
        value = wantActive ? "有效" : "已退课";
    }

    int matched = 0;
    cout << "  ---- 查询结果 ----\n";
    for (const auto& rec : manager_.getRecords()) {
        bool hit = false;
        if (cond == "1") hit = true;
        else if (cond == "2") hit = (rec.studentId == value);
        else if (cond == "3" || cond == "5" || cond == "7") {
            const Student* s = manager_.findStudent(rec.studentId);
            const Course* c = manager_.findCourse(rec.courseId);
            if (cond == "3") hit = (s != nullptr && s->getName().find(value) != string::npos);
            else if (cond == "5")
                hit = (c != nullptr && c->getName().find(value) != string::npos);
            else
                hit = (c != nullptr && c->getTypeName().find(value) != string::npos);
        } else if (cond == "4") hit = (rec.courseId == value);
        else if (cond == "6") hit = (rec.active == wantActive);

        if (hit) {
            cout << "    " << manager_.describeRecord(rec) << "\n";
            ++matched;
        }
    }
    if (matched == 0) cout << "    （未查询到符合条件的报名记录）\n";
    cout << "  共匹配 " << matched << " 条记录。\n";
}

void SystemUI::adminUpdateRecord() {
    cout << "\n--- 修改报名记录 ---\n";
    if (!requireAdmin("修改报名记录")) return;

    manager_.showAllRecords();
    string snStr = ask("请输入要修改的流水号（直接回车取消）", vPositiveInt, true);
    if (snStr.empty()) { cout << "  [提示] 已取消修改。\n"; return; }
    int sn = 0;
    try {
        sn = stoi(snStr);
    } catch (...) {
        cout << "  [失败] 流水号 [" << snStr << "] 超出有效范围。\n";
        return;
    }

    Enrollment* rec = manager_.findRecord(sn);
    if (rec == nullptr) {
        cout << "  [失败] 未找到流水号为 " << sn << " 的报名记录。\n";
        return;
    }
    cout << "  当前记录：" << manager_.describeRecord(*rec) << "\n";

    string sub;
    if (rec->active) {
        cout << "    1) 更换选修课程     2) 将该记录置为已退课     0) 返回\n";
        sub = ask("请选择操作（0-2）", vRequired);
    } else {
        cout << "    3) 恢复为有效报名   0) 返回\n";
        cout << "  [提示] 该记录已退课，仅可恢复。\n";
        sub = ask("请选择操作（0/3）", vRequired);
    }

    if (sub == "0") { cout << "  [提示] 已返回。\n"; return; }

    EnrollStatus st = EnrollStatus::CourseNotFound;
    if (sub == "1") {
        string newCid = ask("请输入新课程编号", vRequired);
        st = manager_.changeRecordCourse(sn, newCid);
    } else if (sub == "2") {
        st = manager_.setRecordActive(sn, false);
    } else if (sub == "3") {
        st = manager_.setRecordActive(sn, true);
    } else {
        cout << "  [错误] 无效的操作选项：" << sub << "\n";
        return;
    }

    if (st == EnrollStatus::Success) {
        cout << "  [成功] 报名记录修改成功！\n";
        persist();
        const Enrollment* updated = manager_.findRecord(sn);
        if (updated != nullptr)
            cout << "  ---- 界面刷新：修改后的记录 ----\n    "
                 << manager_.describeRecord(*updated) << "\n";
        const Student* s = updated != nullptr
                               ? manager_.findStudent(updated->studentId) : nullptr;
        if (s != nullptr) {
            cout << "    该人员有效报名 " << manager_.activeCountOf(s->getId())
                 << " 门，累计 " << s->getSelectedCredits() << " 学分\n";
        }
    } else {
        cout << "  [失败] 报名记录修改失败：" << enrollStatusText(st) << "\n";
    }
}

void SystemUI::adminUpdatePerson() {
    cout << "\n--- 修改人员信息 ---\n";
    if (!requireAdmin("修改人员信息")) return;

    string pid = ask("请输入要修改的学号/工号（直接回车取消）", vRequired, true);
    if (pid.empty()) { cout << "  [提示] 已取消修改。\n"; return; }

    Student* s = manager_.findStudent(pid);
    if (s == nullptr) {
        cout << "  [失败] 学号 " << pid << " 不存在。\n";
        return;
    }
    cout << "  ---- 当前人员档案 ----\n";
    s->displayInfo();

    cout << "    1) 修改姓名   2) 修改专业   3) 修改绩点   4) 修改联系电话   0) 返回\n";
    string sub = ask("请选择要修改的项目（0-4）", vRequired);
    if (sub == "0") { cout << "  [提示] 已返回。\n"; return; }

    if (sub == "1") {
        string name = ask("新的姓名", vName);
        s->setName(name);
        accounts_.renameByPersonId(pid, name);          // 同步账户显示名
    } else if (sub == "2") {
        s->setMajor(ask("新的专业", vMajor));
    } else if (sub == "3") {
        string gpaStr = ask("新的绩点（0.00 - 5.00）", vGpa);
        s->setGpa(stod(gpaStr));
    } else if (sub == "4") {
        s->setPhone(ask("新的联系电话（7-15 位数字）", vPhone));
    } else {
        cout << "  [错误] 无效的操作选项：" << sub << "\n";
        return;
    }

    persist();
    cout << "  [成功] 人员信息修改成功！\n";
    cout << "  ---- 界面刷新：修改后的人员档案 ----\n";
    s->displayInfo();
}

void SystemUI::adminDeletePerson() {
    cout << "\n--- 删除报名人员 ---\n";
    if (!requireAdmin("删除报名人员")) return;

    string pid = ask("请输入要删除的学号/工号（直接回车取消）", vRequired, true);
    if (pid.empty()) { cout << "  [提示] 已取消删除。\n"; return; }

    Student* s = manager_.findStudent(pid);
    if (s == nullptr) {
        cout << "  [失败] 学号 " << pid << " 不存在，无法删除。\n";
        return;
    }
    cout << "  ---- 待删除人员档案 ----\n";
    s->displayInfo();

    int recCount = manager_.recordCountOf(pid);
    bool linked = accounts_.hasPersonId(pid);
    cout << "  [警告] 删除后将同时清理其 " << recCount << " 条报名记录"
         << (linked ? "及关联登录账户" : "") << "，且不可恢复！\n";
    if (!confirm("确认删除该报名人员")) {
        cout << "  [提示] 已取消删除。\n";
        return;
    }

    int removedRecords = 0;
    if (!manager_.removeStudent(pid, removedRecords)) {
        cout << "  [失败] 删除失败。\n";
        return;
    }
    string removedUser;
    if (accounts_.removeByPersonId(pid, removedUser))
        cout << "  [提示] 已同时注销其登录账户：" << removedUser << "\n";

    persist();
    cout << "  [成功] 报名人员已删除（级联清理报名记录 "
         << removedRecords << " 条）！\n";
    cout << "  ---- 界面刷新：剩余人员列表 ----\n";
    manager_.showPersons();                              // 实时更新
}

void SystemUI::adminStatistics() {
    cout << "\n--- 报名统计报表 ---\n";
    if (!requireAdmin("报名统计报表")) return;
    manager_.showStatistics();
}

void SystemUI::adminAllRecords() {
    cout << "\n--- 全部报名流水 ---\n";
    if (!requireAdmin("查看全部报名流水")) return;
    manager_.showAllRecords();
    cout << "\n";
    manager_.showPersons();
}

/*================================ main =================================*/

/**
 * @brief 程序入口：构造课程 / 教师静态数据，启动交互式管理系统
 */
int main() {
    // ---- 三类课程（继承层次的实例化，运行期通过 Course* 多态调用）----
    LectureCourse c101("C101", "高等数学",        "陈晓明", 4, 60,  "周一 1-2 节",  120.0, "教一 101", true);
    LectureCourse c102("C102", "面向对象程序设计", "陈晓明", 3, 40,  "周二 3-4 节",  150.0, "教二 305", false);
    LabCourse     l201("L201", "程序设计基础实验", "刘建国", 2, 2,   "周三 7-8 节",  150.0, 2.5, "实验楼 A302", 200.0);
    LabCourse     l202("L202", "数据库系统实验",   "刘建国", 2, 30,  "周四 9-10 节", 150.0, 3.0, "实验楼 B105", 180.0);
    OnlineCourse  o301("O301", "Python 程序设计",  "陈晓明", 3, 200, "随到随学",     100.0, "慕课网",   50.0, 0.6);
    OnlineCourse  o302("O302", "人工智能导论",     "刘建国", 2, 100, "随到随学",     100.0, "学堂在线", 50.0, 0.6);

    // ---- 两类人员（不同角色的人员层次，经 Person* 多态展示）----
    Teacher t1("T1001", "陈晓明", "教授",   "计算机学院", "13900000001");
    Teacher t2("T1002", "刘建国", "副教授", "计算机学院", "13900000002");
    t1.addCourseId("C101"); t1.addCourseId("C102"); t1.addCourseId("O301");
    t2.addCourseId("L201"); t2.addCourseId("L202"); t2.addCourseId("O302");

    vector<Course*> courses = { &c101, &c102, &l201, &l202, &o301, &o302 };
    vector<Person*> teachers = { &t1, &t2 };

    SystemUI app(courses, teachers);
    app.init();      // 加载持久化数据（首次运行自动初始化）
    app.run();       // 进入菜单主循环
    return 0;
}
